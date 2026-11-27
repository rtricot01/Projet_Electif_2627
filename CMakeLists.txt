cmake_minimum_required(VERSION 3.20)
project(AS_Volley_Project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- dependances

# libpqxx : fichier de config CMake avec vcpkg (Windows), pkg-config sous Linux (Debian/Ubuntu, Docker).
find_package(libpqxx CONFIG QUIET)
if(NOT TARGET libpqxx::pqxx)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
    add_library(libpqxx::pqxx ALIAS PkgConfig::PQXX)
endif()

# ---------------------------------------------------------------- coeur commun
# Les classes metier sont partagees par le programme console et par le site web.

add_library(asvolley_core STATIC
    src/Init.cpp
    src/Db.cpp
    src/Manager.cpp
    src/Utilisateur.cpp
    src/ListeJoueurs.cpp
    src/Equipe.cpp
    src/Entrainement.cpp
    src/Match.cpp
    src/QrCode.cpp
    src/Session.cpp
)
target_include_directories(asvolley_core PUBLIC include)
target_link_libraries(asvolley_core PUBLIC libpqxx::pqxx)

if(MSVC)
    # Les fichiers sources sont en UTF-8.
    target_compile_options(asvolley_core PUBLIC /utf-8)
endif()

# Bibliothèque de QR codes (Nayuki "qrcodegen").
# - Windows / vcpkg : installée automatiquement grâce à vcpkg.json
# - Linux (Debian/Ubuntu) : sudo apt install libqrcodegencpp-dev
# Si elle est absente, le programme compile quand même mais n'affiche pas l'image du QR code.
find_package(nayuki-qr-code-generator CONFIG QUIET)
if(TARGET nayuki-qr-code-generator::qrcodegencpp)
    target_link_libraries(asvolley_core PUBLIC nayuki-qr-code-generator::qrcodegencpp)
    target_compile_definitions(asvolley_core PRIVATE ASV_AVEC_QRCODEGEN)
elseif(TARGET nayuki-qr-code-generator::nayuki-qr-code-generator)
    target_link_libraries(asvolley_core PUBLIC nayuki-qr-code-generator::nayuki-qr-code-generator)
    target_compile_definitions(asvolley_core PRIVATE ASV_AVEC_QRCODEGEN)
else()
    find_library(QRCODEGENCPP_LIB NAMES qrcodegencpp nayuki-qr-code-generator)
    find_path(QRCODEGEN_INCLUDE NAMES qrcodegen.hpp PATH_SUFFIXES qrcodegen)
    if(QRCODEGENCPP_LIB AND QRCODEGEN_INCLUDE)
        target_link_libraries(asvolley_core PUBLIC ${QRCODEGENCPP_LIB})
        target_include_directories(asvolley_core PRIVATE ${QRCODEGEN_INCLUDE})
        target_compile_definitions(asvolley_core PRIVATE ASV_AVEC_QRCODEGEN)
    else()
        message(WARNING "qrcodegen introuvable : les QR codes ne seront pas dessinés.")
    endif()
endif()

# ---------------------------------------------------------------- programme console

add_executable(app src/main.cpp src/Application.cpp)
target_link_libraries(app PRIVATE asvolley_core)

# ---------------------------------------------------------------- site web

# cpp-httplib (serveur HTTP) et nlohmann-json (JSON) : vcpkg sous Windows,
# paquets libcpp-httplib-dev et nlohmann-json3-dev sous Linux.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG QUIET)
if(NOT TARGET httplib::httplib)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
    add_library(httplib::httplib ALIAS PkgConfig::HTTPLIB)
endif()
find_package(Threads REQUIRED)

add_executable(site src/main_site.cpp src/ServeurWeb.cpp)
target_link_libraries(site PRIVATE asvolley_core httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)
