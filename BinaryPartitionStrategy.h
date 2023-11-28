//
// Created by tim on 29.10.23.
//

#ifndef TRANQUILITY_BINARYPARTITIONSTRATEGY_H
#define TRANQUILITY_BINARYPARTITIONSTRATEGY_H

#include <random>
#include "PlayerAgent.h"
#include "GameManager.h"


class BinaryPartitionStrategy : public PlayerAgent {

public:
    explicit BinaryPartitionStrategy(std::default_random_engine &rng, unsigned int player_number) : rng(rng),
                                                                                                    PlayerAgent(
                                                                                                            player_number) {}

    void register_move(unsigned int player_number, Turn turn_made) override {};

    Turn make_turn(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand) override;

    int negotiate_discard_phase(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand,
                                const std::vector<int> current_offer) override;

    Turn perform_discard(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand,
                         const std::vector<int> negotiation_result) override;

private:
    // return delta , pos to play card to play
    std::tuple<int, int, int>
    find_best_middle_card_to_play(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand);

    // return num_discard , pos to play card to play
    std::tuple<int, int, int>
    find_best_adjacent(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand);


    bool is_card_safe_to_discard(const GameManager &GM, int position, const std::vector<std::unique_ptr<Card>> &hand);

    std::vector<int> discarded_values = {};

    int get_num_finish_discarded() {
        return std::accumulate(discarded_values.begin(), discarded_values.end(), 0, [](auto accu, auto v) {
            return accu + (v == Card::FINISH);
        });
    }

    std::default_random_engine &rng;

    int discard_safety_margin = 1;
    int num_unsafe_turns = 0;// for statistics

    int discard_negotiation_round = 0;

    static const int draw_size_difference = 2;

};


#endif //TRANQUILITY_BINARYPARTITIONSTRATEGY_H
