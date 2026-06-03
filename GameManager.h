#ifndef TRANQUILITY_GAMEMANAGER_H
#define TRANQUILITY_GAMEMANAGER_H

#include <memory>
#include <vector>
#include <algorithm>
#include <cassert>
#include <numeric>

#include "PlayArea.h"
#include "PlayerAgent.h"
#include "Player.h"

//TODO implement solo rules?
#define MIN_PLAYER_COUNT 2
#define MAX_PLAYER_COUNT 5

#define NUM_DISCARD_DISCARD_PHASE 8

struct Turn;

enum class Winnability { Won, Lost, Unknown };

class GameManager {

public:
    GameManager() = delete;

    GameManager(GameManager &) = delete;

    PlayArea area = PlayArea();

    size_t get_hand_size(unsigned int player_number) const {
        assert(player_number < players.size());
        return players[player_number]->hand.size();
    }

    size_t get_deck_size(unsigned int player_number) const {
        assert(player_number < players.size());
        return players[player_number]->draw.size();
    }

    size_t get_discard_size(unsigned int player_number) const {
        assert(player_number < players.size());
        return players[player_number]->discard.size();
    }

    size_t get_num_players() const {
        return players.size();
    }

    int get_num_finish_in_total_deck() const {
        //TODO refactroring: use same constant when creating the deck
        return 5;
    }

    const std::vector<std::unique_ptr<Card> > &get_hand(unsigned int player_number) const {
        assert(player_number < players.size());
        return players[player_number]->hand;
    }

    const std::vector<std::unique_ptr<Card> > &get_draw(unsigned int player_number) const {
        assert(player_number < players.size());
        return players[player_number]->draw;
    }

    // Winnability oracle. Perfect-info collusion semantics: assumes a colluding
    // pair of players who can see every hand and draw pile in fixed order.
    // Won     : a complete legal play sequence ending in FINISH was found.
    // Lost    : the sound lower-bound check proved no completion exists.
    // Unknown : node_budget exhausted before a verdict.
    // `current_player` is whose turn the search starts at — pass the calling
    // agent's player_number when invoking from inside make_turn; leave as 0
    // for between-turn calls (e.g., pre-game filtering).
    // 2-player only for v1. See Winnability.cpp for algorithm details.
    Winnability is_winnable(uint64_t node_budget = 5'000'000,
                            unsigned int current_player = 0) const;

    template<class R>
    static bool RunNewGame(std::vector<std::unique_ptr<PlayerAgent>> strategies, R &rng);

private:
    explicit GameManager(std::vector<std::unique_ptr<Player>> players) : players(std::move(players)) {

    }

    std::vector<int> run_discard_phase_negotiation();

    void run_discard_phase_execution(const std::vector<int> &negotiation_result);

    // return if won
    bool run_game();

    std::vector<std::unique_ptr<Player>> players;

};

struct Turn {
    bool has_lost = true;
    std::vector<unsigned int> cards_to_discard;
    int card_to_play = -1;
    int position_played = -1;
    bool is_discard_phase = false;

    bool is_valid(const PlayArea &area, const std::vector<std::unique_ptr<Card>> &hand);

};

template<class R>
bool GameManager::RunNewGame(std::vector<std::unique_ptr<PlayerAgent>> strategies, R &rng) {
    auto num_players = strategies.size();
    assert(MIN_PLAYER_COUNT <= num_players);
    assert(num_players <= MAX_PLAYER_COUNT);

    auto deck = create_deck();

    std::shuffle(std::begin(deck), std::end(deck), rng);

    std::vector<std::unique_ptr<Player>> players;

    unsigned int cards_per_player = deck.size() / num_players;
    auto reminder = deck.size() % num_players;

    for (int i = 0; i < num_players; ++i) {
        unsigned int cards_this_player = cards_per_player;
        if (reminder > 0) {
            cards_this_player++;
            reminder--;
        }
        std::vector<std::unique_ptr<Card>> this_players_deck;
        this_players_deck.reserve(cards_this_player + 1);

        this_players_deck.push_back(std::make_unique<Card>(Card::START));
        for (int j = 0; j < cards_this_player; ++j) {
            this_players_deck.push_back(std::move(deck.back()));
            deck.pop_back();
        }
        std::shuffle(std::begin(this_players_deck), std::end(this_players_deck), rng);

        assert(strategies[i]->player_number == i);
        players.push_back(std::make_unique<Player>(i, std::move(this_players_deck), std::move(strategies[i])));
    }
    auto GM = GameManager(std::move(players));
    return GM.run_game();
}

inline int num_start_in_hand(const std::vector<std::unique_ptr<Card>> &hand) {
    return std::accumulate(hand.begin(), hand.end(), 0, [](auto accu, const auto &c) {
        if (c->value == Card::START) {
            return accu + 1;
        } else { return accu; }
    });
}


#endif //TRANQUILITY_GAMEMANAGER_H
