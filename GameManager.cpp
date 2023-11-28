
#include "GameManager.h"

#include <cassert>
#include <numeric>


std::vector<int> GameManager::run_discard_phase_negotiation() {

    std::vector<int> current_offer(players.size(), -1);
    std::vector<int> new_offer(players.size(), -1);

    // operator + is default for accumulate
    while (std::accumulate(current_offer.begin(), current_offer.end(), 0) != NUM_DISCARD_DISCARD_PHASE) {
        for (auto &p: players) {
            new_offer[p->player_number] = p->strategy->negotiate_discard_phase(*this, p->hand, current_offer);
        }
        current_offer = new_offer;
    }
    return current_offer;
}

void GameManager::run_discard_phase_execution(const std::vector<int> &negotiation_result) {

    for (auto &p: players) {
        Turn discard_turn = p->strategy->perform_discard(*this, p->hand, negotiation_result);
        assert(discard_turn.is_valid(area, p->hand));
        assert(discard_turn.cards_to_discard.size() == negotiation_result[p->player_number]);
        // perform the discard
        //TODO bad smell: code duplication
        for (auto d: discard_turn.cards_to_discard) {
            p->discard.push_back(std::move(p->hand[d]));
            p->hand.erase(p->hand.begin() + d);
        }
        // register turn with other players, so they know what is going on
        for (auto &other_p: players) {
            if (p != other_p) {// no need to register with itself
                other_p->strategy->register_move(p->player_number, discard_turn);
            }
        }
        p->draw_to_hand_size();
    }
}

bool GameManager::run_game() {

    while (true) {
        for (auto &p: players) {
            auto turn = p->strategy->make_turn(*this, p->hand);
            assert(turn.is_valid(area, p->hand));
            if (turn.has_lost) {
                return false;// lost
            }
            // track if start card was played
            bool enter_discard_phase = false;
            // execute turn
            if (turn.card_to_play != -1) {
                enter_discard_phase = p->hand[turn.card_to_play]->value == Card::START;
                area.play_card(turn.position_played, std::move(p->hand[turn.card_to_play]));
                p->hand.erase(p->hand.begin() + turn.card_to_play);
            }
            for (auto d: turn.cards_to_discard) {
                p->discard.push_back(std::move(p->hand[d]));
                p->hand.erase(p->hand.begin() + d);
            }
            // register turn with other players, so they know what is going on
            for (auto &other_p: players) {
                if (p != other_p) {// no need to register with itself
                    other_p->strategy->register_move(p->player_number, turn);
                }
            }
            p->draw_to_hand_size();

            if (area.has_finish()) {
                return true; // won
            }
            if (enter_discard_phase) {
                if (players.size() == 2) {
                    // both players draw 2 cards before discard phase
                    for (int i = 0; i < 2; ++i) {
                        players[0]->hand.push_back(std::move(players[0]->draw.back()));
                        players[0]->draw.pop_back();
                        players[1]->hand.push_back(std::move(players[1]->draw.back()));
                        players[1]->draw.pop_back();
                    }
                    auto negotiation_result = run_discard_phase_negotiation();
                    run_discard_phase_execution(negotiation_result);
                }
            }
        }
    }
}

bool Turn::is_valid(const PlayArea &area, const std::vector<std::unique_ptr<Card>> &hand) {

    if (is_discard_phase) {
        if (hasDuplicates(cards_to_discard)) {
            return false;
        }
        for (auto c: cards_to_discard) {
            if (c >= hand.size()) { return false; }
        }
        // the Game Manager should check if the Agent honors the negotiation Result
        return true;
    }

    assert(not is_discard_phase);

    if (hand.empty()) {
        return has_lost;// must loose
    }

    if (has_lost) {
        // check if a different turn was possible
        if (hand.size() > 1) {
            // could discard
            return false;
        } else {
            // check if there is a large enough gap to play just one card
            for (int i = 0; i < PlayArea::LENGTH; ++i) {
                if (area.get_num_discard(i, hand[0]->value) == 0) {
                    // could play at this position
                    return false;
                }
            }
        }
        return true;
    }


    //all cards to discard must be different
    if (hasDuplicates(cards_to_discard)) {
        return false;
    }

    // dont play just discard
    if (card_to_play == -1) {
        if (cards_to_discard.size() == 2) {
            return cards_to_discard[0] < hand.size() && cards_to_discard[1] < hand.size();
        } else { return false; }
    }

    if (card_to_play > hand.size()) {
        return false;
    }

    assert(hand[card_to_play] != nullptr);

    // if at least one start card in hand and not played so far
    if (area.has_start() && num_start_in_hand(hand) > 0) {
        return (hand[card_to_play]->value == Card::START &&
                cards_to_discard.empty());
    }
    if (hand[card_to_play]->value == Card::START && area.has_start()) { return false; }

    // test for Finish is implemented in get_num_discard

    // discarded correct amount of cards
    int num_discard = area.get_num_discard(position_played, hand[card_to_play]->value);
    if (num_discard == -1) {
        return false;
    }
    if (num_discard == cards_to_discard.size()) {
        for (auto c: cards_to_discard) {
            if (c >= hand.size() || c == card_to_play) { return false; }
        }
        return true;
    }

    return false;
}

