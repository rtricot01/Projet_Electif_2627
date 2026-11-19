#include "Manager.h"

#include <utility>

#include "Utilisateur.h"

AccesRefuse::AccesRefuse(const std::string& action)
    : std::runtime_error("Acces refuse : seul un admin peut " + action + ".")
{
}

Manager::Manager(std::string connexion) : m_connexion(std::move(connexion)) {}

void Manager::exigerAdmin(const Utilisateur& auteur, const std::string& action)
{
    if (!auteur.estAdmin()) {
        throw AccesRefuse(action);
    }
}
