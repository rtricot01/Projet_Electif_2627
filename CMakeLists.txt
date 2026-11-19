cmake_minimum_required(VERSION 3.20)
project(AS_Volley_Project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libpqxx CONFIG REQUIRED)

# Fichiers sources à compiler
set(SOURCES
    src/main.cpp
    src/Init.cpp
    src/User.cpp
)

# Création de l'exécutable
add_executable(app ${SOURCES})
target_include_directories(app PRIVATE include)

target_link_libraries(app PRIVATE libpqxx::pqxx)