#ifndef MANAGER_H_INCLUDED
#define MANAGER_H_INCLUDED

#include <stdexcept>
#include <string>

class Utilisateur;

// Exception levee quand un utilisateur essaie une action reservee aux admins.
class AccesRefuse : public std::runtime_error {
public:
    explicit AccesRefuse(const std::string& action);
};

// Exception levee quand une donnee saisie est invalide (date, score...).
class DonneeInvalide : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Classe mere de tous les "Manager" : ils gardent l'adresse de la base
// et savent verifier qu'un utilisateur a les droits admin.
class Manager {
protected:
    std::string m_connexion;

    // Leve AccesRefuse si l'auteur de l'action n'est pas admin.
    static void exigerAdmin(const Utilisateur& auteur, const std::string& action);

public:
    explicit Manager(std::string connexion);
    virtual ~Manager() = default;
};

#endif // MANAGER_H_INCLUDED
