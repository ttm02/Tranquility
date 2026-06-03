//
// Created by tim on 29.10.23.
//

#include "BinaryPartitionStrategy.h"
#include "GameManager.h"
#include "HumanPlayer.h"

#include <iostream>


// See GameManager::is_winnable (Winnability.cpp) for the oracle that
// answers whether the current position admits a winning play sequence
// under perfect-information collusion. Used below to flag avoidable
// losses when this strategy gives up.


#define PRINT_TURNS

// Run the winnability oracle on every has_lost branch to flag avoidable
// losses. The oracle is expensive (seconds per call); leave off for normal
// simulation, switch on for debugging.
constexpr bool DEBUG_AVOIDABLE_LOSS = false;

// Call the oracle before every turn to trace when the position transitions
// from winnable to unwinnable — the turn at which that flip happens is the
// move that broke the game.
constexpr bool DEBUG_TRACE_WINNABILITY = false;
// Trace mode only invokes the oracle in the DFS window (gaps ≤ 12), where
// it's exact. A few million nodes is enough for late-game DFS to converge
// most of the time; raise this if many turns return Unknown.
constexpr uint64_t TRACE_WINNABILITY_BUDGET = 5'000'000;


//Problem: one player ran out of cards, The other player had finish and the required missing card (including spare cards to fit it in) in hand
Turn BinaryPartitionStrategy::make_turn(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand) {
    if (DEBUG_TRACE_WINNABILITY) {
        static int trace_turn = 0;
        ++trace_turn;
        int gaps = GM.area.get_num_gaps();
        // Beam search at early/mid game is too slow per call to run on every
        // turn. Only query the oracle when DFS will run (gaps ≤ 12), so each
        // call is fast and the answer is exact Won/Lost.
        if (gaps <= 12) {
            auto w = GM.is_winnable(TRACE_WINNABILITY_BUDGET, player_number);
            const char *label = (w == Winnability::Won)
                                    ? "Won"
                                    : (w == Winnability::Lost)
                                          ? "Lost"
                                          : "Unknown";
            std::cout << "[oracle] turn " << trace_turn << " player " << player_number
                    << " cells_left=" << gaps << ": " << label << std::endl;
        } else {
            std::cout << "[oracle] turn " << trace_turn << " player " << player_number
                    << " cells_left=" << gaps << ": skipped (out of DFS window)" << std::endl;
        }
    }

    //DEBUG:
    print_hand(hand);
    GM.area.print();

    //TODO extract this as utility to base class
    Turn turn;
    turn.has_lost = true;
    if (turn.is_valid(GM.area, hand)) {
        if (hand.size() <= 1) {
            std::cout << "Run out of cards\n";
            //std::cout << "Cards Left: ";
            //for (int i = 0; i < GM.get_num_players(); ++i) {
            //    std::cout << GM.get_deck_size(i) + GM.get_hand_size(i) << ", ";
            //}
            //std::cout << "\n";
        }
        std::cout << "HAS LOST\n";
        if (DEBUG_AVOIDABLE_LOSS) {
            if (GM.is_winnable(200'000, player_number) == Winnability::Won) {
                std::cout << "[oracle] avoidable loss detected\n";
            }
        }
        return turn;
    }
    turn.has_lost = false;

    if (not GM.area.has_start() && num_start_in_hand(hand) >= 1) {
        for (int i = 0; i < hand.size(); ++i) {
            if (hand[i]->value == Card::START) {
                turn.card_to_play = i;
                return turn;
            }

        }
    }

    if (GM.area.get_num_gaps() == 0) {
        for (int i = 0; i < hand.size(); ++i) {
            if (hand[i]->value == Card::FINISH) {
                turn.card_to_play = i;
                return turn;
            }
        }
    }
    // END Check for lost, start, finish



// debugging why one player rn out of cards before the other
/*
    if (player_number==0){
        std::cout << "Draw sizes: ";
        for (int i = 0; i < GM.get_num_players(); ++i) {
            std::cout << GM.get_deck_size(i) << ", ";

        }
        std::cout << "\n";
    }
*/



    // idea find the position that is "as much middle as possible"
    // if possible: fill in gaps if a card is safe to discard

    // when no "middle" gap exists anymore: play the cheapest card, discard 2 if it becomes too expansive

    auto middle_gap = find_best_middle_card_to_play(GM, hand);

    // LB-aware fill-gap selection: lowest-cost (card, position) play with
    // cost > 0 whose post-play state still passes the LB feasibility check
    // (i.e., the move doesn't lock in an unwinnable pool/grid combo). Falls
    // back to the original cheapest-regardless of feasibility if nothing
    // passes — that lets the agent still make SOME move when in a corner.
    auto fill_gap = find_best_adjacent(GM, hand); {
        int best_cost = std::numeric_limits<int>::max();
        int best_pos = -1;
        int best_card_idx = -1;
        for (int pos = 0; pos < PlayArea::LENGTH; ++pos) {
            for (int j = 0; j < (int) hand.size(); ++j) {
                int cost = GM.area.get_num_discard(pos, hand[j]->value);
                if (cost <= 0) continue;
                if (cost >= best_cost) continue;
                if (!GM.is_post_turn_lb_feasible(hand[j]->value, pos, {})) continue;
                best_cost = cost;
                best_pos = pos;
                best_card_idx = j;
            }
        }
        if (best_pos != -1) {
            fill_gap = std::make_tuple(best_cost, best_pos, best_card_idx);
        }
    }

#ifdef PRINT_TURNS
    std::cout << "Safe to discard: ";
#endif
    int num_safe_discards = 0;
    for (int i = 0; i < hand.size(); ++i) {
        if (is_card_safe_to_discard(GM, i, hand)) {
            num_safe_discards++;
#ifdef  PRINT_TURNS
            std::cout << hand[i]->value << ", ";
#endif
        }
    }
#ifdef PRINT_TURNS
    std::cout << "\n";
#endif

    // Behind on draw at all: any other player has strictly more remaining
    // draw cards. The old check required a 2-card lead by everyone; that was
    // too strict — under symmetric play, players stay within 1–2 cards of each
    // other so the threshold never trips. Loosening it to "behind by any"
    // gives the agent a chance to slow down whenever its opponent has buried
    // cards still to dig out.
    bool has_lowest_number_of_cards = false;
    for (int i = 0; i < GM.get_num_players(); ++i) {
        if (i != player_number && GM.get_deck_size(player_number) < GM.get_deck_size(i)) {
            has_lowest_number_of_cards = true;
            break;
        }
    }

    // Always prefer a "fill middle" turn when available: it places a card at
    // a cell whose immediate neighbors are still empty (cost = 0), burning only
    // ONE card from the pool that turn. Fill-gap plays cost 1+discards, which
    // depletes the pool ≥ twice as fast. The original "only when behind" gate
    // was too narrow — under symmetric play, both players stayed within 1–2
    // cards and the gate never tripped. By always preferring middle when one
    // is available, both players slow their pool consumption, which gives the
    // game enough turns for buried low values to surface from the deeper
    // player's draw pile. has_lowest_number_of_cards is kept around for the
    // potential heuristic value of biasing the *middle pick* itself, but the
    // gate is removed.
    (void) has_lowest_number_of_cards;
    bool should_perform_middle_turn = std::get<1>(middle_gap) != -1;
    if (should_perform_middle_turn) {
#ifdef PRINT_TURNS
        std::cout << "middle position (pool-preserve): " << std::get<1>(middle_gap)
                << " Card To Play: " << hand[std::get<2>(middle_gap)]->value << "\n";
#endif
        turn.position_played = std::get<1>(middle_gap);
        turn.card_to_play = std::get<2>(middle_gap);
        return turn;
    }


    if (num_safe_discards >= std::get<0>(fill_gap) && not should_perform_middle_turn) {
#ifdef PRINT_TURNS
        std::cout << "fill gap position: " << std::get<1>(fill_gap) << " Card To Play: "
                  << hand[std::get<2>(fill_gap)]->value
                  << " Discard: ";
#endif
        // can safetly fill a gap
        // fill a gap turn
        turn.position_played = std::get<1>(fill_gap);
        turn.card_to_play = std::get<2>(fill_gap);
        int num_to_discard = GM.area.get_num_discard(turn.position_played, hand[turn.card_to_play]->value);
        int played_value = hand[turn.card_to_play]->value;

        auto already_picked = [&](int i) {
            return std::find(turn.cards_to_discard.begin(), turn.cards_to_discard.end(),
                             (unsigned) i) != turn.cards_to_discard.end();
        };
        auto try_add = [&](int i) -> bool {
            std::vector<int> dv;
            for (auto idx: turn.cards_to_discard) dv.push_back(hand[idx]->value);
            dv.push_back(hand[i]->value);
            if (!GM.is_post_turn_lb_feasible(played_value, turn.position_played, dv)) return false;
            turn.cards_to_discard.push_back(i);
            discarded_values.push_back(hand[i]->value);
#ifdef PRINT_TURNS
            std::cout << hand[i]->value << ", ";
#endif
            return true;
        };

        // Phase 1: locally-safe AND keeps LB ≤ pool_budget.
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (i == turn.card_to_play || already_picked(i)) continue;
            if (!is_card_safe_to_discard(GM, i, hand)) continue;
            try_add(i);
        }
        // Phase 2: relax local-safety, still require LB feasibility.
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (i == turn.card_to_play || already_picked(i)) continue;
            try_add(i);
        }
        // Phase 3: original fallback — locally-safe regardless of LB (in case
        // the LB rejects every option and we still need to play something).
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (i == turn.card_to_play || already_picked(i)) continue;
            if (!is_card_safe_to_discard(GM, i, hand)) continue;
            turn.cards_to_discard.push_back(i);
            discarded_values.push_back(hand[i]->value);
#ifdef PRINT_TURNS
            std::cout << hand[i]->value << ", ";
#endif
        }
#ifdef PRINT_TURNS
        std::cout << "\n";
#endif
        return turn;
    }

    // fill a middle turn
    if (std::get<1>(middle_gap) != -1) {
#ifdef PRINT_TURNS
        std::cout << "middle position: " << std::get<1>(middle_gap) << " Card To Play: "
                  << hand[std::get<2>(middle_gap)]->value << "\n";
#endif

        turn.position_played = std::get<1>(middle_gap);
        turn.card_to_play = std::get<2>(middle_gap);

        // stop to see what is happening
        //std::cout << "Confirm Turn ";
        //std::cin.get();// wait for enter
        //std::cout << "\n";

        return turn;

    }

    if (hand.size() >= 2) {
        // Voluntary discard 2: do it whenever the hand has ≥ 2 cards. Previously
        // gated on `num_safe_discards >= 2`, which left no fallback when zero
        // cards were locally-safe — the agent then hit the "Invalid Turn" path.
        // With the LB-aware discard selection below (and the truly-unconditional
        // phase 3), discarding 2 is always at least as good as crashing.
#ifdef PRINT_TURNS
        std::cout << "Discard 2: ";
#endif
        const int num_to_discard = 2;
        auto already_picked = [&](int i) {
            return std::find(turn.cards_to_discard.begin(), turn.cards_to_discard.end(),
                             (unsigned) i) != turn.cards_to_discard.end();
        };
        auto try_add = [&](int i) -> bool {
            std::vector<int> dv;
            for (auto idx: turn.cards_to_discard) dv.push_back(hand[idx]->value);
            dv.push_back(hand[i]->value);
            if (!GM.is_post_turn_lb_feasible(/*played_value=*/0, /*position=*/-1, dv)) return false;
            turn.cards_to_discard.push_back(i);
            discarded_values.push_back(hand[i]->value);
#ifdef PRINT_TURNS
            std::cout << hand[i]->value << ", ";
#endif
            return true;
        };

        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (already_picked(i)) continue;
            if (!is_card_safe_to_discard(GM, i, hand)) continue;
            try_add(i);
        }
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (already_picked(i)) continue;
            if (hand[i]->value == Card::FINISH) continue; // don't volunteer FINISH
            try_add(i);
        }
        // Phase 3: truly unconditional fallback. We've committed to discarding 2;
        // if neither local-safety nor LB feasibility lets us pick enough, take
        // whatever cards are left (still excluding FINISH). Without this the
        // turn would be invalid and the game would crash.
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (already_picked(i)) continue;
            if (hand[i]->value == Card::FINISH) continue;
            turn.cards_to_discard.push_back(i);
            discarded_values.push_back(hand[i]->value);
#ifdef PRINT_TURNS
            std::cout << hand[i]->value << ", ";
#endif
        }
        // Phase 4: true emergency — allow FINISH if hand has only FINISH cards
        // left to fill the 2-discard quota. The rules force a discard turn when
        // hand.size() > 1 and no legal play exists; otherwise the validator
        // rejects the turn. We'd rather burn a FINISH (other FINISHes may still
        // be in the shared pool) than crash.
        for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
            if (already_picked(i)) continue;
            turn.cards_to_discard.push_back(i);
            discarded_values.push_back(hand[i]->value);
#ifdef PRINT_TURNS
            std::cout << hand[i]->value << "(!), ";
#endif
        }
#ifdef PRINT_TURNS
        std::cout << "\n";
#endif
        return turn;
    }


    num_unsafe_turns++;
    std::cout << "Invalid Turn:\n";

    // stop to see what is happening
    std::cout << "Confirm Turn ";

    std::cin.get();// wait for enter
    std::cout << "\n";

    return turn;
}


int
BinaryPartitionStrategy::negotiate_discard_phase(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand,
                                                 const std::vector<int> current_offer) {
    discard_negotiation_round++;
#ifdef PRINT_TURNS
    if (discard_negotiation_round == 1) {
        std::cout << "Discard Phase: safe to discard: ";
    }
#endif

    //TODO code duplication
    int num_safe_discards = 0;
    for (int i = 0; i < hand.size(); ++i) {
        if (is_card_safe_to_discard(GM, i, hand)) {
            num_safe_discards++;
#ifdef PRINT_TURNS
            if (discard_negotiation_round == 1) {
                std::cout << hand[i]->value << ", ";
            }
#endif
        }
    }
#ifdef PRINT_TURNS
    if (discard_negotiation_round == 1) {
        std::cout << "\n";
    }
#endif
    if (discard_negotiation_round == 1) {
        return num_safe_discards > 0 ? 1 : 0;
    }

    int my_offer = current_offer[player_number];
    // operator + is default for accumulate
    // agreed on discarding too much
    if (std::accumulate(current_offer.begin(), current_offer.end(), 0) > NUM_DISCARD_DISCARD_PHASE) {

        if (discard_negotiation_round % GM.get_num_players() == player_number || my_offer > num_safe_discards) {
            my_offer--;
        }
        return my_offer;
    }
    // not enough
    assert(std::accumulate(current_offer.begin(), current_offer.end(), 0) < NUM_DISCARD_DISCARD_PHASE);

    if (my_offer < num_safe_discards) { return my_offer + 1; }


    int min_delta = 0; // delta to self is 0
    for (auto offer: current_offer) {
        min_delta = std::min(min_delta, my_offer - offer);
    }
    if (std::abs(min_delta) >= draw_size_difference) {
        return my_offer + 1;
        // dont grow the difference between offers too large
    }

    if (discard_negotiation_round % GM.get_num_players() == player_number &&
        discard_negotiation_round > GM.get_num_players() * 10) {
        // negotiation takes to long: we need to discard more even if not safe to do so
        return my_offer + 1;
    }

    return my_offer;

}

Turn BinaryPartitionStrategy::perform_discard(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand,
                                              const std::vector<int> negotiation_result) {

    Turn turn;
    turn.is_discard_phase = true;
    //TODO code duplication
    int num_safe_discards = 0;
    for (int i = 0; i < hand.size(); ++i) {
        if (is_card_safe_to_discard(GM, i, hand)) {
            num_safe_discards++;
        }
    }
    int num_to_discard = negotiation_result[player_number];

    std::cout << num_to_discard << "\n";

    auto already_picked = [&](int i) {
        return std::find(turn.cards_to_discard.begin(), turn.cards_to_discard.end(),
                         (unsigned) i) != turn.cards_to_discard.end();
    };
    auto try_add = [&](int i) -> bool {
        std::vector<int> dv;
        for (auto idx: turn.cards_to_discard) dv.push_back(hand[idx]->value);
        dv.push_back(hand[i]->value);
        if (!GM.is_post_turn_lb_feasible(/*played_value=*/0, /*position=*/-1, dv)) return false;
        turn.cards_to_discard.push_back(i);
        discarded_values.push_back(hand[i]->value);
        return true;
    };

    // Phase 1: locally-safe AND LB-feasible.
    for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
        if (already_picked(i)) continue;
        if (!is_card_safe_to_discard(GM, i, hand)) continue;
        try_add(i);
    }
    // Phase 2: relax local-safety, keep LB-feasibility.
    for (int i = 0; i < hand.size() && (int) turn.cards_to_discard.size() < num_to_discard; ++i) {
        if (already_picked(i)) continue;
        if (hand[i]->value == Card::FINISH) continue; // never volunteer the last FINISH
        try_add(i);
    }
    // Phase 3: original fallback — random non-FINISH cards.
    while ((int) turn.cards_to_discard.size() < num_to_discard) {
        int to_discard = rng() % hand.size();
        if (already_picked(to_discard)) continue;
        if (hand[to_discard]->value == Card::FINISH) continue;
        turn.cards_to_discard.push_back(to_discard);
        discarded_values.push_back(hand[to_discard]->value);
    }
    return turn;
}


// binary search: best position for the given card inside of a larger gap
// start pos and end pos are inclusive
int find_pos_for_card(int card_value, int start_pos, int end_pos, int start_value, int end_value) {

    assert(start_pos <= end_pos);
    assert(start_value <= end_value);

    int length = end_pos - start_pos + 1;
    int middle_value = start_value + (end_value - start_value) / 2;
    int middle_pos = start_pos + length / 2;

    if (start_value == end_value) {
        return middle_pos;
    }

    if (length == 1) {
        assert(start_pos == end_pos);
        return start_pos;
    }
    if (length == 2) {
        return card_value < middle_value ? start_pos : end_pos;
    }

    int left_pos = find_pos_for_card(card_value, start_pos, middle_pos, start_value, middle_value);
    int right_pos = find_pos_for_card(card_value, middle_pos, end_pos, middle_value, end_value);
    // both times the middle is included
    // meaning that one time the middle pos will be "best" i.e. in the wrong array side
    // or both are middle
    return left_pos == middle_pos ? right_pos : left_pos;
}


std::tuple<int, int, int> BinaryPartitionStrategy::find_best_middle_card_to_play(const GameManager &GM,
                                                                                 const std::vector<std::unique_ptr<Card>> &hand) {

    unsigned int previous_played_pos = 0;
    unsigned int current_best_pos = -1;
    unsigned int current_card_to_play = -1;
    unsigned int current_best_delta = std::numeric_limits<int>::max();
    int current_best_flexibility = std::numeric_limits<int>::max();

    // Number of grid positions on which this value can legally be placed
    // right now (cost ≥ 0). Lower count = card is more "constrained" —
    // fewer alternative homes — so we'd rather place it now than risk it
    // becoming unplaceable as neighbors fill in. Used as a tiebreaker on
    // equal delta in middle-play candidate selection.
    auto count_legal_positions = [&](int value) {
        int count = 0;
        for (int p = 0; p < PlayArea::LENGTH; ++p) {
            if (GM.area.get_num_discard(p, value) >= 0) count++;
        }
        return count;
    };

    for (int i = 0; i <= PlayArea::LENGTH; ++i) {
        if (GM.area.get_area()[i] != nullptr || i == PlayArea::LENGTH) {

            if (i - previous_played_pos > 3) {
                // otherwise: no space to play in between without adjacency

                unsigned current_pos = i;
                // i == LENGTH is the virtual right-edge sentinel — there is no
                // card past the end of the area, so reading get_area()[LENGTH]
                // would be out of bounds (the vector is exactly LENGTH long).
                // Treat that case as "no right neighbor" (val_right = sentinel,
                // placement allowed up to LENGTH-1).
                bool right_is_edge = (i == PlayArea::LENGTH);
                unsigned val_right = Card::MAX_VALUE + 1;
                if (!right_is_edge && GM.area.get_area()[i] != nullptr) {
                    val_right = GM.area.get_area()[i]->value;
                }
                unsigned val_left = 1 - 1;
                if (GM.area.get_area()[previous_played_pos] != nullptr) {
                    val_left = GM.area.get_area()[previous_played_pos]->value;
                }
                for (int j = 0; j < hand.size(); ++j) {
                    // positions -2 to leave space for the adjacend card (will be handled in different method)
                    // therefore we also subtract 2 from value search space
                    //but the endings needs to be included fully
                    int left_begin_search =
                            GM.area.get_area()[previous_played_pos] != nullptr ? previous_played_pos + 2 : 0;
                    int right_begin_search;
                    if (right_is_edge) {
                        // Open-ended right gap: positions 0..LENGTH-1 are valid;
                        // current_pos itself (== LENGTH) is not a playable cell.
                        right_begin_search = PlayArea::LENGTH - 1;
                    } else {
                        right_begin_search = GM.area.get_area()[current_pos] != nullptr ? current_pos - 2 : current_pos;
                    }

                    int best_pos = find_pos_for_card(hand[j]->value,
                                                     left_begin_search, right_begin_search,
                                                     val_left + 2, val_right - 2);
                    int val_diff = val_right - val_left;
                    int pos_diff = current_pos - previous_played_pos;
                    double value_per_step = ((double) val_diff) /
                                            ((double) pos_diff);
                    int approx_value_required = val_left + (int) (value_per_step *
                                                                  (double) (best_pos - previous_played_pos));
                    int delta = std::abs(hand[j]->value - approx_value_required);

                    int new_flexibility = count_legal_positions(hand[j]->value);

                    // Lex compare on (delta, flexibility): smaller delta wins;
                    // on equal delta, the more constrained card (fewer legal
                    // positions on the grid) wins. The latter is what gets
                    // hard-to-place values out of the hand before the only
                    // slot that fits them is sealed off by adjacent placements.
                    bool better =
                            delta < (int) current_best_delta ||
                            (delta == (int) current_best_delta &&
                             new_flexibility < current_best_flexibility);

                    if (better
                        && val_left < hand[j]->value &&
                        hand[j]->value < val_right
                            ) {
                        // check if this move invalidates the board
                        // i.e. not enough possible cards fo fill the resulting gaps
                        bool is_valid =
                                // left side
                                hand[j]->value - val_left >= best_pos - previous_played_pos &&
                                // right side
                                val_right - hand[j]->value >= current_pos - best_pos;
                        if (is_valid) {
                            current_best_delta = delta;
                            current_best_flexibility = new_flexibility;
                            current_card_to_play = j;
                            current_best_pos = best_pos;
                        }
                    }
                }
            }
            previous_played_pos = i;
        }
    }

    //std::cout << "Best Delta for middle Play: " << current_best_delta << "\n";

    return std::make_tuple(current_best_delta, current_best_pos, current_card_to_play);


}


std::tuple<int, int, int>
BinaryPartitionStrategy::find_best_adjacent(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand) {


    unsigned int current_best_pos = -1;
    unsigned int current_card_to_play = -1;
    unsigned int current_best_num_discard = std::numeric_limits<int>::max();

    for (int i = 0; i < PlayArea::LENGTH; ++i) {
        for (int j = 0; j < hand.size(); ++j) {
            int new_num_discard = GM.area.get_num_discard(i, hand[j]->value);
            if (new_num_discard > 0 && new_num_discard < current_best_num_discard) {
                current_best_num_discard = new_num_discard;
                current_card_to_play = j;
                current_best_pos = i;
            }
        }

    }


    //std::cout << "Best discard to adjacent: " << current_best_num_discard << "\n";
    return std::make_tuple(current_best_num_discard, current_best_pos, current_card_to_play);

}

bool BinaryPartitionStrategy::is_card_safe_to_discard(const GameManager &GM, int position,
                                                      const std::vector<std::unique_ptr<Card>> &hand) {
    // it is safe if:

    int card_value = hand[position]->value;
    // start is not needed anymore
    if (card_value == Card::START) {
        return GM.area.has_start();
    }

    //TODO finish?
    // is safe if we have multiple in hand or if we know we have a finish card left in draw:
    // if we havent discarded NUM_Finish/num_players yet
    //TODO or if we have multiple Finish cards in hand
    if (card_value == Card::FINISH) {
        for (int i = position + 1; i < hand.size(); ++i) {
            if (hand[i]->value == Card::FINISH) {
                return true;
            }
        }
        // only if last finish in hand:
        int num_already_discarded = get_num_finish_discarded();
        for (int i = position - 1; i > 0; --i) {
            if (hand[i]->value == Card::FINISH) {
                num_already_discarded++;// will at some point discard the other finish cards in hand
            }
        }

        int allowed_to_discard = GM.get_num_finish_in_total_deck() / GM.get_num_players(); // num discards per player
        if (GM.get_num_finish_in_total_deck() % GM.get_num_players() == 0 &&
            GM.get_num_players() == player_number - 1) {
            // may not discard the last finish if last player
            allowed_to_discard--;
        }

        return num_already_discarded < allowed_to_discard;
    }

    int larger = -1;
    int smaller = -1;

    for (int i = 0; i < PlayArea::LENGTH; ++i) {
        if (GM.area.get_area()[i] != nullptr) {
            int value_in_field = GM.area.get_area()[i]->value;
            if (value_in_field < card_value) {
                smaller = i;
            }
            if (value_in_field > card_value) {
                larger = i;
                break;
            }
        }
    }

    //we know that the safety margin may increase when the negotiate discard phase happens
    // it may also be the case that it increases when other has only unsafe cards but must play
    if (larger != -1 && smaller != -1) {
        if (larger - smaller == 1) {
            // no more gaps in between
            //immedeately adjacent
            return true;
        }
        if (larger - smaller == 2) {
            //only one space left
            return GM.area.get_area()[smaller]->value + discard_safety_margin < card_value &&
                   card_value < GM.area.get_area()[larger]->value - discard_safety_margin;
        }
        if (larger - smaller == 3) {
            //two spaces left
            // use slightly higher discard margin to be safe
            return GM.area.get_area()[smaller]->value + discard_safety_margin + 1 < card_value &&
                   card_value < GM.area.get_area()[larger]->value - (discard_safety_margin + 1);
        }
    }

    if (smaller == -1 && larger != -1) {
        // so small it will not be played anymore
        if (larger == 0) {
            return true;
        }
        if (larger == 1) {
            return card_value < GM.area.get_area()[larger]->value - discard_safety_margin;
        }
    }

    if (smaller != -1 && larger == -1) {
        // so large it will not be played anymore
        if (smaller == PlayArea::LENGTH - 1) {
            return true;
        }
        if (smaller == PlayArea::LENGTH - 2) {
            return card_value > GM.area.get_area()[smaller]->value + discard_safety_margin;
        }
    }

    return false;

}