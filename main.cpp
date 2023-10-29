#include <iostream>

#include <utility>
#include <vector>
#include <memory>

#include <algorithm>
#include <random>
#include <iomanip>
#include <cassert>
#include <limits>

//TODO implement special rules for less players
#define MIN_PLAYER_COUNT 3
#define MAX_PLAYER_COUNT 5

class Card {
public:
    const unsigned int value;

    // max value is inclusive
    static const unsigned int MAX_VALUE = 80;

    // special value
    static const unsigned int START = 100;

    static const unsigned int FINISH = 101;


    explicit Card(unsigned int v) : value(v) {};

    // not copy able
    Card(const Card &) = delete;

    // default destructor

};

const unsigned int Card::MAX_VALUE;
const unsigned int Card::START;
const unsigned int Card::FINISH;


std::vector<std::unique_ptr<Card>> create_deck() {
    std::vector<std::unique_ptr<Card>> deck;

    deck.reserve(100);
    for (int i = 0; i < 5; ++i) {
        //deck.push_back(std::make_unique<Card>(Card::START));// each player gets one start card
        deck.push_back(std::make_unique<Card>(Card::FINISH));
    }
    for (unsigned int i = 0 + 1; i < Card::MAX_VALUE + 1; ++i) {
        deck.push_back(std::make_unique<Card>(i));
    }

    return deck;

};

class GameManager;

class PlayArea {
public:
    PlayArea() {
        for (int i = 0; i < 6; ++i) {
            area.push_back(std::vector<std::unique_ptr<Card>>(6));
            // nullptr is default
        }
    }

    PlayArea(PlayArea &) = delete;

private:
    std::vector<std::vector<std::unique_ptr<Card>>> area;
public:
    const std::vector<std::vector<std::unique_ptr<Card>>> &get_area() const { return area; };

    void play_card(std::pair<int, int> pos, std::unique_ptr<Card> card) {

        assert(card != nullptr);
        assert(get_num_discard(pos, card->value) != -1);

        if (card->value == Card::START) {
            has_start = true;
            return;
        }
        if (card->value == Card::FINISH) {
            has_finish = true;
            return;
        }
        area[pos.first][pos.second] = std::move(card);
        return;
    }

    // return -1 if move is invalid
    int get_num_discard(std::pair<int, int> pos, unsigned int new_card_value) const {
        if (new_card_value == Card::START) {
            // check if valid
            if (not has_start) {
                return 0;
            }
            {
                return -1;
            }
        }
        if (new_card_value == Card::FINISH) {
            auto num_gaps = std::accumulate(area.begin(), area.end(), 0, [](auto accu, const auto &row) {
                return accu + std::accumulate(row.begin(), row.end(), 0, [](auto accu, const auto &card) {
                    if (card == nullptr) {
                        return accu + 1;
                    } else {
                        return accu + 0;
                    }
                });
            });
            if (not has_finish && num_gaps == 0) {
                return 0;
            } else {
                return -1;
            }
        }

        // position is in Boundaries and free
        if (pos.first >= 0 && pos.first < HEIGHT && pos.second >= 0 && pos.second < WIDTH &&
            area[pos.first][pos.second] == nullptr) {

            std::pair<int, int> left_neighbor = pos;
            if (pos != std::make_pair(0, 0)) {
                if (pos.second == 0) {
                    left_neighbor = std::make_pair(pos.first - 1, WIDTH);
                } else {
                    left_neighbor = std::make_pair(pos.first, pos.second - 1);
                }
            }
            std::pair<int, int> right_neighbor = pos;
            if (pos != std::make_pair(5, 5)) {
                if (pos.second == WIDTH - 1) {
                    left_neighbor = std::make_pair(pos.first + 1, 0);
                } else {
                    left_neighbor = std::make_pair(pos.first, pos.second + 1);
                }
            }
            if (area[right_neighbor.first][right_neighbor.second] == nullptr &&
                area[left_neighbor.first][left_neighbor.second] == nullptr) {
                // no neighbor
                return 0;
            } else {
                unsigned int num_discard = std::numeric_limits<int>::max();

                if (area[right_neighbor.first][right_neighbor.second] != nullptr) {
                    if (area[right_neighbor.first][right_neighbor.second]->value < new_card_value) {
                        return -1;
                    }
                    num_discard = std::min(num_discard,
                                           area[right_neighbor.first][right_neighbor.second]->value - new_card_value);
                }
                if (area[left_neighbor.first][left_neighbor.second] != nullptr) {
                    if (new_card_value < area[left_neighbor.first][left_neighbor.second]->value) {
                        return -1;
                    }
                    num_discard = std::min(new_card_value - num_discard,
                                           area[left_neighbor.first][left_neighbor.second]->value);
                }
                assert(num_discard <= std::numeric_limits<int>::max());
                return static_cast<int>(num_discard);
            }

        } else {
            return -1; // invalid
        }
    }

    bool has_start = false;
    bool has_finish = false;

    static const unsigned int WIDTH = 6;
    static const unsigned int HEIGHT = 6;

    void print() const {
        // first row
        if (has_start) {
            std::cout << " S";
        } else {
            std::cout << "  ";
        }
        for (int i = 0; i < WIDTH; ++i) {
            std::cout << "\t" << std::setw(2) << i;
        }
        std::cout << "\n";

        for (int i = 0; i < HEIGHT; ++i) {
            std::cout << std::setw(2) << i;
            for (int j = 0; j < WIDTH; ++j) {
                if (area[i][j]) {
                    std::cout << "\t" << std::setw(2) << area[i][j]->value;
                } else {
                    std::cout << "\t" << "  ";
                }
            }
            std::cout << "\t" << std::setw(2) << i;
            std::cout << "\n";

        }

        std::cout << "  ";
        for (int i = 0; i < WIDTH; ++i) {
            std::cout << "\t" << std::setw(2) << i;
        }
        if (has_finish) {
            std::cout << "\t" << " F";
        } else {
            std::cout << "\t" << "  ";
        }
        std::cout << "\n";
    }

};

class PlayerStrategy;

class Player {
public:
    Player() = delete;

    Player(unsigned int player_number, std::vector<std::unique_ptr<Card>> draw_pile,
           std::unique_ptr<PlayerStrategy> strategy) :
            player_number(player_number),
            draw(std::move(draw_pile)),
            strategy(std::move(strategy)) {

        draw_to_hand_size();
    }

    Player(Player &) = delete;

    void draw_to_hand_size() {
        while (hand.size() < 5 && !draw.empty()) {
            hand.push_back(std::move(draw.back()));
            draw.pop_back();
        }
    }

    std::vector<std::unique_ptr<Card>> draw;
    std::vector<std::unique_ptr<Card>> discard;
    std::vector<std::unique_ptr<Card>> hand;

    const unsigned int player_number;
    std::unique_ptr<PlayerStrategy> strategy;

};

struct Turn {
    bool has_lost = true;
    std::vector<unsigned int> cards_to_discard;
    int card_to_play = -1;
    std::pair<int, int> position_played = {-1, -1};
    bool is_discard_phase = false;

    bool is_valid(const PlayArea &area, const std::vector<std::unique_ptr<Card>> &hand) {
        bool valid = true;
        if (has_lost) {
            // check if there was a possible turn

        }
        if (hand.empty()) {
            return has_lost;// must loose
        }
    }

};

class PlayerStrategy {
public:
    explicit PlayerStrategy(unsigned int player_number) : player_number(player_number) {}

    PlayerStrategy(PlayerStrategy &) = delete;

    virtual void register_move(unsigned int player_number, Turn turn_made) = 0;

    virtual Turn make_turn(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand) = 0;

    virtual int negotiate_discard_phase(const GameManager &GM, const std::vector<int> current_offer) = 0;

    virtual Turn perform_discard(const GameManager &GM, const std::vector<int> negotiation_result) = 0;

    const unsigned int player_number;
};

class GameManager {

public:
    GameManager() = delete;

    GameManager(GameManager &) = delete;

    PlayArea area = PlayArea();

    size_t get_hand_size(unsigned int player_number) {
        assert(player_number < players.size());
        return players[player_number]->hand.size();
    }

    size_t get_deck_size(unsigned int player_number) {
        assert(player_number < players.size());
        return players[player_number]->draw.size();
    }

    size_t get_discard_size(unsigned int player_number) {
        assert(player_number < players.size());
        return players[player_number]->discard.size();
    }

    size_t get_num_players() {
        return players.size();
    }

    template<class R>
    static bool RunNewGame(std::vector<std::unique_ptr<PlayerStrategy>> strategies, R &rng) {
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

private:
    explicit GameManager(std::vector<std::unique_ptr<Player>> players) : players(std::move(players)) {

    }

    // return won
    bool run_game() {
        return false;
    }

    std::vector<std::unique_ptr<Player>> players;

};

class HumanPlayer : public PlayerStrategy {

public:
    explicit HumanPlayer(unsigned int player_number) : PlayerStrategy(player_number) {}

    void register_move(unsigned int player_number, Turn turn_made) override {};

    Turn make_turn(const GameManager &GM, const std::vector<std::unique_ptr<Card>> &hand) override {

        Turn turn_to_make;
        //TODO implement
        turn_to_make.has_lost = true;
        return turn_to_make;
    }

    int negotiate_discard_phase(const GameManager &GM, const std::vector<int> current_offer) override {
        assert(false);
        //TODO implement
    }

    Turn perform_discard(const GameManager &GM, const std::vector<int> negotiation_result) override {
        assert(false);
        //TODO implement
    }
};


int main() {

    //TODO specify RNG to use as parameter?
    auto rng = std::default_random_engine{};

    std::vector<std::unique_ptr<PlayerStrategy>> strategies;

    for (int i = 0; i < 3; ++i) {
        strategies.push_back(std::make_unique<HumanPlayer>(i));
    }

    bool game_won = GameManager::RunNewGame(std::move(strategies), rng);


    std::cout << "Game Won? " << game_won << "\n";


    return 0;

}
