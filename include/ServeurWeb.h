#ifndef SERVEURWEB_H_INCLUDED
#define SERVEURWEB_H_INCLUDED

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Entrainement.h"
#include "Equipe.h"
#include "Match.h"
#include "Session.h"
#include "Utilisateur.h"

namespace httplib {
class Server;
struct Request;
struct Response;
}

// Serveur du site web : une API JSON (/api/...) utilisee par les pages du dossier "web/".
// Toutes les regles (droits admin, verifications) restent dans les classes *Manager,
// exactement comme pour le programme console.
class ServeurWeb {
public:
    ServeurWeb(const std::string& connexion, std::string dossierWeb);
    ~ServeurWeb();

    // Demarre le serveur (bloque jusqu'a l'arret du programme).
    bool demarrer(const std::string& hote, int port);

private:
    // Ce que recoit chaque route : la requete, la reponse et le membre connecte.
    struct Contexte {
        const httplib::Request& requete;
        httplib::Response& reponse;
        std::optional<Utilisateur> membre;

        const Utilisateur& connecte() const;   // leve une erreur 401 si personne n'est connecte
        const Utilisateur& admin() const;      // leve AccesRefuse si le membre n'est pas admin
        int parametre(std::size_t numero) const; // nombre lu dans l'adresse (ex : /api/matchs/12)
    };
    using Action = std::function<void(Contexte&)>;

    void route(const std::string& methode, const std::string& motif, Action action);
    void declarerRoutes();

    std::string m_dossierWeb;
    std::unique_ptr<httplib::Server> m_serveur;
    UtilisateurManager m_utilisateurs;
    SessionManager m_sessions;
    EquipeManager m_equipes;
    EntrainementManager m_entrainements;
    MatchManager m_matchs;
};

#endif // SERVEURWEB_H_INCLUDED
