cmake_minimum_required(VERSION 3.20)
project(AS_Volley_Project CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libpqxx CONFIG REQUIRED)

# Fichiers sources à compiler
set(SOURCES
    src/main.cpp
    src/Init.cpp
    src/Db.cpp
    src/Manager.cpp
    src/Utilisateur.cpp
    src/ListeJoueurs.cpp
    src/Equipe.cpp
    src/Entrainement.cpp
    src/Match.cpp
    src/QrCode.cpp
    src/Application.cpp
)

# Création de l'exécutable
add_executable(app ${SOURCES})
target_include_directories(app PRIVATE include)

target_link_libraries(app PRIVATE libpqxx::pqxx)

if(MSVC)
    # Les fichiers sources sont en UTF-8.
    target_compile_options(app PRIVATE /utf-8)
endif()

# Bibliothèque de QR codes (Nayuki "qrcodegen").
# - Windows / vcpkg : installée automatiquement grâce à vcpkg.json
# - Linux (Debian/Ubuntu) : sudo apt install libqrcodegencpp-dev
# Si elle est absente, le programme compile quand même mais n'affiche pas l'image du QR code.
find_package(nayuki-qr-code-generator CONFIG QUIET)
if(TARGET nayuki-qr-code-generator::qrcodegencpp)
    target_link_libraries(app PRIVATE nayuki-qr-code-generator::qrcodegencpp)
    target_compile_definitions(app PRIVATE ASV_AVEC_QRCODEGEN)
elseif(TARGET nayuki-qr-code-generator::nayuki-qr-code-generator)
    target_link_libraries(app PRIVATE nayuki-qr-code-generator::nayuki-qr-code-generator)
    target_compile_definitions(app PRIVATE ASV_AVEC_QRCODEGEN)
else()
    find_library(QRCODEGENCPP_LIB NAMES qrcodegencpp nayuki-qr-code-generator)
    find_path(QRCODEGEN_INCLUDE NAMES qrcodegen.hpp PATH_SUFFIXES qrcodegen)
    if(QRCODEGENCPP_LIB AND QRCODEGEN_INCLUDE)
        target_link_libraries(app PRIVATE ${QRCODEGENCPP_LIB})
        target_include_directories(app PRIVATE ${QRCODEGEN_INCLUDE})
        target_compile_definitions(app PRIVATE ASV_AVEC_QRCODEGEN)
    else()
        message(WARNING "qrcodegen introuvable : les QR codes ne seront pas dessinés.")
    endif()
endif()
