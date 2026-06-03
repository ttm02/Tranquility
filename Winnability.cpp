#include "GameManager.h"
#include "Card.h"
#include "PlayArea.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_set>
#include <vector>

namespace {
    constexpr int GRID_LEN = 36;
    constexpr int HAND_MAX = 5;
    constexpr int DFS_CUTOFF_CELLS = 12;
    constexpr int BEAM_WIDTH = 256;

    struct SearchState {
        std::array<int, GRID_LEN> area{};
        bool has_start = false;
        bool has_finish = false;
        int remaining_discard_phase = 0;
        int current_player = 0;

        struct PlayerView {
            std::vector<int> hand;
            std::vector<int> draw_remaining;
        };

        std::array<PlayerView, 2> p;

        int cells_remaining() const {
            int n = 0;
            for (int v: area) if (v == 0) ++n;
            return n;
        }

        int pool_size() const {
            return static_cast<int>(p[0].hand.size() + p[0].draw_remaining.size() +
                                    p[1].hand.size() + p[1].draw_remaining.size());
        }

        // Cards available to spend on gap-discards + voluntary discards + leftover.
        // Subtracts the unavoidable consumers: grid cells, the START / FINISH plays
        // (each consumes one card from pool), and the impending discard-phase quota.
        int pool_budget() const {
            int overhead = cells_remaining() +
                           (has_start ? 0 : 1) +
                           (has_finish ? 0 : 1) +
                           remaining_discard_phase;
            return pool_size() - overhead;
        }
    };

    SearchState from_game(const GameManager &gm, unsigned int current_player) {
        SearchState s;
        auto const &area = gm.area.get_area();
        for (int i = 0; i < GRID_LEN; ++i) {
            s.area[i] = area[i] ? area[i]->value : 0;
        }
        s.has_start = gm.area.has_start();
        s.has_finish = gm.area.has_finish();
        s.current_player = static_cast<int>(current_player);
        for (int pi = 0; pi < 2; ++pi) {
            for (auto const &c: gm.get_hand(pi)) s.p[pi].hand.push_back(c->value);
            for (auto const &c: gm.get_draw(pi)) s.p[pi].draw_remaining.push_back(c->value);
        }
        return s;
    }

    bool is_island(int v) { return v >= 1 && v <= Card::MAX_VALUE; }

    int placement_cost(const SearchState &s, int pos, int v) {
        if (pos < 0 || pos >= GRID_LEN) return -1;
        if (s.area[pos] != 0) return -1;
        int left = (pos > 0) ? s.area[pos - 1] : 0;
        int right = (pos < GRID_LEN - 1) ? s.area[pos + 1] : 0;
        if (left == 0 && right == 0) return 0;
        int best = std::numeric_limits<int>::max();
        if (right != 0) {
            if (right < v) return -1;
            best = std::min(best, right - v);
        }
        if (left != 0) {
            if (v < left) return -1;
            best = std::min(best, v - left);
        }
        return best;
    }

    // Sound lower bound on remaining gap-discard cost.
    // Walk the grid as a sequence of "islands" (maximal runs of empty cells) and
    // for each island count the number of cells that CANNOT be free (must be
    // placed with at least one already-filled neighbor). Each such cell pays ≥ 1.
    // A cell is free only if it can be placed when both its neighbors are still
    // empty — so any cell adjacent to a pre-filled cell (the boundary of an
    // island) is never free, and among interior cells of an island only an
    // independent set can be free. Singleton islands between two pre-filled
    // neighbors get a tighter exact-pool minimum-cost contribution. Returns
    // INT_MAX/2 if any island has fewer in-range pool values than empty cells.
    int lower_bound_gap_cost(const SearchState &s) {
        std::vector<int> pool;
        for (int pi = 0; pi < 2; ++pi) {
            for (int v: s.p[pi].hand) if (is_island(v)) pool.push_back(v);
            for (int v: s.p[pi].draw_remaining) if (is_island(v)) pool.push_back(v);
        }
        std::sort(pool.begin(), pool.end());

        int total = 0;
        int i = 0;
        while (i < GRID_LEN) {
            if (s.area[i] != 0) {
                ++i;
                continue;
            }
            int start = i;
            while (i < GRID_LEN && s.area[i] == 0) ++i;
            int N = i - start;
            int L = (start > 0) ? s.area[start - 1] : 0;
            int R = (i < GRID_LEN) ? s.area[i] : 0;
            int boundaries = (L != 0 ? 1 : 0) + (R != 0 ? 1 : 0);

            int low_v = (L != 0) ? L + 1 : 1;
            int high_v = (R != 0) ? R - 1 : Card::MAX_VALUE;
            if (high_v < low_v) return std::numeric_limits<int>::max() / 2;
            auto lo_it = std::lower_bound(pool.begin(), pool.end(), low_v);
            auto hi_it = std::upper_bound(pool.begin(), pool.end(), high_v);
            int available = static_cast<int>(hi_it - lo_it);
            if (available < N) return std::numeric_limits<int>::max() / 2;

            int contribution;
            if (N == 1 && boundaries == 2) {
                int min_cost = std::numeric_limits<int>::max();
                for (auto it = lo_it; it != hi_it; ++it) {
                    int cost = std::min(*it - L, R - *it);
                    if (cost < min_cost) min_cost = cost;
                    if (min_cost == 1) break;
                }
                contribution = min_cost;
            } else {
                int paid;
                if (boundaries == 2) paid = N - (N - 1) / 2;
                else if (boundaries == 1) paid = N - N / 2;
                else paid = N / 2;
                contribution = paid;
            }
            total += contribution;
        }
        return total;
    }

    struct Move {
        enum Kind { Normal, DiscardPhaseMove };

        Kind kind = Normal;
        int player = 0;
        int played_idx = -1;
        int played_value = 0;
        int position = -1;
        std::vector<int> discards;
        std::vector<int> discards_p1;
        bool is_start_play = false;
        bool is_finish_play = false;
    };

    void draw_up(SearchState::PlayerView &pv) {
        while (pv.hand.size() < HAND_MAX && !pv.draw_remaining.empty()) {
            pv.hand.push_back(pv.draw_remaining.back());
            pv.draw_remaining.pop_back();
        }
    }

    void apply_move(SearchState &s, const Move &m) {
        if (m.kind == Move::DiscardPhaseMove) {
            for (auto it = m.discards.rbegin(); it != m.discards.rend(); ++it)
                s.p[0].hand.erase(s.p[0].hand.begin() + *it);
            for (auto it = m.discards_p1.rbegin(); it != m.discards_p1.rend(); ++it)
                s.p[1].hand.erase(s.p[1].hand.begin() + *it);
            draw_up(s.p[0]);
            draw_up(s.p[1]);
            s.remaining_discard_phase = 0;
            return;
        }
        int p = m.player;
        std::vector<int> all = m.discards;
        if (m.played_idx >= 0) all.push_back(m.played_idx);
        std::sort(all.begin(), all.end());
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            s.p[p].hand.erase(s.p[p].hand.begin() + *it);
        if (m.is_start_play) {
            s.has_start = true;
        } else if (m.is_finish_play) {
            s.has_finish = true;
        } else if (m.played_value > 0 && m.position >= 0) {
            s.area[m.position] = m.played_value;
        }
        draw_up(s.p[p]);
        if (m.is_start_play) {
            // 2P: both players draw +2 (or what's available) before negotiation
            for (int i = 0; i < 2; ++i) {
                for (int j = 0; j < 2; ++j) {
                    if (!s.p[j].draw_remaining.empty()) {
                        s.p[j].hand.push_back(s.p[j].draw_remaining.back());
                        s.p[j].draw_remaining.pop_back();
                    }
                }
            }
            s.remaining_discard_phase = NUM_DISCARD_DISCARD_PHASE;
        }
        s.current_player = 1 - p;
    }

    void enumerate_normal_moves(const SearchState &s, std::vector<Move> &out) {
        int p = s.current_player;
        auto const &hand = s.p[p].hand;
        int hand_size = static_cast<int>(hand.size());
        if (hand_size == 0) return;

        int start_idx = -1;
        for (int i = 0; i < hand_size; ++i) {
            if (hand[i] == Card::START) {
                start_idx = i;
                break;
            }
        }
        if (start_idx != -1 && !s.has_start) {
            Move m;
            m.player = p;
            m.played_idx = start_idx;
            m.played_value = Card::START;
            m.is_start_play = true;
            out.push_back(std::move(m));
            return;
        }

        int cells_left = s.cells_remaining();
        for (int ci = 0; ci < hand_size; ++ci) {
            int v = hand[ci];
            if (v == Card::START) continue;
            if (v == Card::FINISH) {
                if (cells_left == 0 && s.has_start) {
                    Move m;
                    m.player = p;
                    m.played_idx = ci;
                    m.played_value = Card::FINISH;
                    m.is_finish_play = true;
                    out.push_back(std::move(m));
                }
                continue;
            }
            for (int pos = 0; pos < GRID_LEN; ++pos) {
                int cost = placement_cost(s, pos, v);
                if (cost < 0) continue;
                int avail = hand_size - 1;
                if (cost > avail) continue;
                std::vector<int> remaining_indices;
                remaining_indices.reserve(hand_size - 1);
                for (int j = 0; j < hand_size; ++j) if (j != ci) remaining_indices.push_back(j);

                std::vector<int> combo;
                std::function < void(int, int) > rec = [&](int start, int need) {
                    if (need == 0) {
                        Move m;
                        m.player = p;
                        m.played_idx = ci;
                        m.played_value = v;
                        m.position = pos;
                        m.discards = combo;
                        std::sort(m.discards.begin(), m.discards.end());
                        out.push_back(std::move(m));
                        return;
                    }
                    if ((int) remaining_indices.size() - start < need) return;
                    for (int i = start; i < (int) remaining_indices.size(); ++i) {
                        combo.push_back(remaining_indices[i]);
                        rec(i + 1, need - 1);
                        combo.pop_back();
                    }
                };
                rec(0, cost);
            }
        }

        if (hand_size >= 2) {
            for (int i = 0; i < hand_size; ++i) {
                for (int j = i + 1; j < hand_size; ++j) {
                    Move m;
                    m.player = p;
                    m.played_idx = -1;
                    m.discards = {i, j};
                    out.push_back(std::move(m));
                }
            }
        }
    }

    void enumerate_discard_phase_moves(const SearchState &s, std::vector<Move> &out) {
        int total_to_discard = s.remaining_discard_phase;
        int hand0 = static_cast<int>(s.p[0].hand.size());
        int hand1 = static_cast<int>(s.p[1].hand.size());

        for (int a = 0; a <= hand0 && a <= total_to_discard; ++a) {
            int b = total_to_discard - a;
            if (b < 0 || b > hand1) continue;

            std::vector<int> combo_a, combo_b;
            std::function < void(int) > rec_b = [&](int start) {
                if ((int) combo_b.size() == b) {
                    Move m;
                    m.kind = Move::DiscardPhaseMove;
                    m.discards = combo_a;
                    m.discards_p1 = combo_b;
                    std::sort(m.discards.begin(), m.discards.end());
                    std::sort(m.discards_p1.begin(), m.discards_p1.end());
                    out.push_back(std::move(m));
                    return;
                }
                for (int i = start; i < hand1; ++i) {
                    combo_b.push_back(i);
                    rec_b(i + 1);
                    combo_b.pop_back();
                }
            };
            std::function < void(int) > rec_a = [&](int start) {
                if ((int) combo_a.size() == a) {
                    rec_b(0);
                    return;
                }
                for (int i = start; i < hand0; ++i) {
                    combo_a.push_back(i);
                    rec_a(i + 1);
                    combo_a.pop_back();
                }
            };
            rec_a(0);
        }
    }

    std::vector<Move> enumerate_legal_turns(const SearchState &s) {
        std::vector<Move> out;
        if (s.has_finish) return out;
        if (s.remaining_discard_phase > 0) {
            enumerate_discard_phase_moves(s, out);
        } else {
            enumerate_normal_moves(s, out);
        }
        return out;
    }

    // --- DFS ---

    Winnability dfs_search(SearchState s, uint64_t &expansions, uint64_t budget) {
        if (s.has_finish) return Winnability::Won;
        if (++expansions > budget) return Winnability::Unknown;
        if (lower_bound_gap_cost(s) > s.pool_budget()) return Winnability::Lost;

        auto moves = enumerate_legal_turns(s);
        if (moves.empty()) return Winnability::Lost;

        bool any_unknown = false;
        for (auto const &m: moves) {
            SearchState child = s;
            apply_move(child, m);
            auto r = dfs_search(std::move(child), expansions, budget);
            if (r == Winnability::Won) return Winnability::Won;
            if (r == Winnability::Unknown) any_unknown = true;
        }
        return any_unknown ? Winnability::Unknown : Winnability::Lost;
    }

    // --- Beam ---

    int dead_card_count(const SearchState &s) {
        int dead = 0;
        int p = s.current_player;
        for (int v: s.p[p].hand) {
            if (v == Card::START || v == Card::FINISH) continue;
            bool fits = false;
            for (int pos = 0; pos < GRID_LEN; ++pos) {
                int cost = placement_cost(s, pos, v);
                if (cost >= 0 && cost + 1 <= (int) s.p[p].hand.size()) {
                    fits = true;
                    break;
                }
            }
            if (!fits) ++dead;
        }
        return dead;
    }

    int fast_lb(const SearchState &s) {
        int total = 0;
        for (int i = 0; i < GRID_LEN; ++i) {
            if (s.area[i] != 0) continue;
            int left = (i > 0) ? s.area[i - 1] : 0;
            int right = (i < GRID_LEN - 1) ? s.area[i + 1] : 0;
            if (left != 0 && right != 0) total += 1;
        }
        return total;
    }

    int cells_filled(const SearchState &s) { return GRID_LEN - s.cells_remaining(); }

    uint64_t state_hash(const SearchState &s) {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
        for (int v: s.area) mix((uint64_t) v);
        mix(s.has_start ? 1 : 0);
        mix(s.has_finish ? 1 : 0);
        mix((uint64_t) s.remaining_discard_phase);
        mix((uint64_t) s.current_player);
        for (int pi = 0; pi < 2; ++pi) {
            std::vector<int> hs = s.p[pi].hand;
            std::sort(hs.begin(), hs.end());
            for (int v: hs) mix((uint64_t) v);
            mix(0xdead);
            std::vector<int> ds = s.p[pi].draw_remaining;
            std::sort(ds.begin(), ds.end());
            for (int v: ds) mix((uint64_t) v);
            mix(0xbeef);
        }
        return h;
    }

    int score_state(const SearchState &s) {
        int score = 100 * cells_filled(s)
                    + 10 * (s.pool_budget() - fast_lb(s))
                    - 5 * dead_card_count(s)
                    - 2 * (s.has_start ? 0 : 5);
        score += static_cast<int>(state_hash(s) % 7u);
        return score;
    }

    Winnability beam_search(SearchState s0, uint64_t budget) {
        std::vector<SearchState> layer;
        layer.push_back(std::move(s0));
        uint64_t spent = 0;

        while (!layer.empty()) {
            std::vector<std::pair<int, SearchState> > next;
            for (auto const &s: layer) {
                if (s.has_finish) return Winnability::Won;
                if (++spent > budget) return Winnability::Unknown;

                auto moves = enumerate_legal_turns(s);
                int dead_p = (s.remaining_discard_phase == 0) ? dead_card_count(s) : 0;

                for (auto const &m: moves) {
                    if (m.kind == Move::Normal && m.played_idx == -1 && m.discards.size() == 2) {
                        if (dead_p == 0) continue;
                    }
                    SearchState child = s;
                    apply_move(child, m);
                    if (child.has_finish) return Winnability::Won;
                    if (lower_bound_gap_cost(child) > child.pool_budget()) continue;
                    int sc = score_state(child);
                    next.emplace_back(sc, std::move(child));
                }
            }

            if (next.empty()) return Winnability::Unknown;

            std::sort(next.begin(), next.end(),
                      [](auto const &a, auto const &b) { return a.first > b.first; });

            std::unordered_set<uint64_t> seen;
            layer.clear();
            for (auto &pr: next) {
                uint64_t h = state_hash(pr.second);
                if (seen.insert(h).second) {
                    layer.push_back(std::move(pr.second));
                    if ((int) layer.size() >= BEAM_WIDTH) break;
                }
            }
        }

        return Winnability::Unknown;
    }
} // anonymous namespace

bool GameManager::is_post_turn_lb_feasible(int played_value, int position,
                                           const std::vector<int> &discard_values) const {
    if (get_num_players() != 2) return true; // helper only supports 2P
    SearchState s = from_game(*this, 0);

    auto pop_value = [&](int v) {
        for (int pi = 0; pi < 2; ++pi) {
            auto &h = s.p[pi].hand;
            auto it = std::find(h.begin(), h.end(), v);
            if (it != h.end()) {
                h.erase(it);
                return;
            }
            auto &d = s.p[pi].draw_remaining;
            auto it2 = std::find(d.begin(), d.end(), v);
            if (it2 != d.end()) {
                d.erase(it2);
                return;
            }
        }
    };

    if (played_value != 0) {
        pop_value(played_value);
        if (played_value == Card::START) {
            s.has_start = true;
            s.remaining_discard_phase = NUM_DISCARD_DISCARD_PHASE;
        } else if (played_value == Card::FINISH) {
            s.has_finish = true;
        } else if (position >= 0 && position < GRID_LEN) {
            s.area[position] = played_value;
        }
    }
    for (int v: discard_values) pop_value(v);

    return lower_bound_gap_cost(s) <= s.pool_budget();
}

Winnability GameManager::is_winnable(uint64_t node_budget, unsigned int current_player) const {
    if (get_num_players() != 2) {
        // TODO: 3-5 player support. Discard-phase logic is 2P-specific.
        return Winnability::Unknown;
    }
    assert(current_player < 2);

    SearchState s = from_game(*this, current_player);
    if (s.has_finish) return Winnability::Won;
    if (lower_bound_gap_cost(s) > s.pool_budget()) return Winnability::Lost;

    if (s.cells_remaining() <= DFS_CUTOFF_CELLS) {
        uint64_t expansions = 0;
        return dfs_search(std::move(s), expansions, node_budget);
    } else {
        return beam_search(std::move(s), node_budget * 7 / 8);
    }
}
