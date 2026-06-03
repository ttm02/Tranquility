cmake_minimum_required(VERSION 3.20)
project(tranquility)

set(CMAKE_CXX_STANDARD 17)

add_executable(tranquility main.cpp
        GameManager.h
        GameManager.cpp
        PlayArea.cpp
        PlayArea.h
        Card.h
        Card.cpp
        Player.cpp
        Player.h
        PlayerAgent.h
        HumanPlayer.cpp
        HumanPlayer.h
        BinaryPartitionStrategy.cpp
        BinaryPartitionStrategy.h
        Winnability.cpp)
