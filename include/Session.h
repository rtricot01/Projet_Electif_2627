#ifndef SESSION_H_INCLUDED
#define SESSION_H_INCLUDED

#include <optional>
#include <string>

#include "Manager.h"
#include "Utilisateur.h"

// Sessions du site web : quand un membre se connecte, il recoit un jeton secret
// (stocke dans un cookie). Le jeton permet de le reconnaitre aux requetes suivantes.
// Seule une empreinte (SHA-256) du jeton est gardee dans la base.
class SessionManager : public Manager {
public:
    // Duree de validite d'une session (30 jours).
    static constexpr int DUREE_JOURS = 30;

    explicit SessionManager(std::string connexion);

    // Ouvre une session pour ce membre et renvoie le jeton a mettre dans le cookie.
    std::string ouvrir(int utilisateurId);

    // Retrouve le membre connecte a partir du jeton (rien si le jeton est inconnu ou expire).
    std::optional<Utilisateur> utilisateur(const std::string& jeton);

    // Deconnexion.
    void fermer(const std::string& jeton);
};

#endif // SESSION_H_INCLUDED
