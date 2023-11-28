#include <iostream>

#include <utility>
#include <vector>
#include <memory>

#include <algorithm>
#include <random>
#include <iomanip>
#include <cassert>
#include <limits>

#include "GameManager.h"
#include "HumanPlayer.h"
#include "BinaryPartitionStrategy.h"

template<class R>
bool simulate_game(R &rng, unsigned int num_players) {

    std::vector<std::unique_ptr<PlayerAgent>> strategies;

    for (int i = 0; i < 2; ++i) {
        strategies.push_back(std::make_unique<BinaryPartitionStrategy>(rng, i));
    }

    bool game_won = GameManager::RunNewGame(std::move(strategies), rng);

    return game_won;

}


int main() {

    // TODO use a seed
    auto rng = std::default_random_engine{};
    int num_games = 100;

    int num_won = 0;
    for (int i = 0; i < num_games; ++i) {

        bool game_won = simulate_game(rng, 2);

        std::cout << "Game " << i << " Won? " << game_won << "\n";
        if (game_won) { num_won++; }
    }


    std::cout << "Won " << num_won << " of " << num_games << "\n";
    return 0;

}


