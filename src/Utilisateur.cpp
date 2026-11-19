#include "Utilisateur.h"

#include <iostream>
#include <pqxx/pqxx>
#include <utility>

#include "Db.h"

using namespace std;

const string Utilisateur::PREFIXE_QR = "ASVOLLEY:";

string statutVersTexte(Statut statut)
{
    return statut == Statut::Admin ? "admin" : "client";
}

Statut statutDepuisTexte(const string& texte)
{
    return texte == "admin" ? Statut::Admin : Statut::Client;
}

// ---------------------------------------------------------------- Utilisateur

// Liste d'initialisation : les attributs sont remplis directement.
Utilisateur::Utilisateur(int id, string numeroLicence, string nom, string prenom,
                         string filiere, Statut statut, string qrToken)
    : m_id(id),
      m_numeroLicence(std::move(numeroLicence)),
      m_nom(std::move(nom)),
      m_prenom(std::move(prenom)),
      m_filiere(std::move(filiere)),
      m_statut(statut),
      m_qrToken(std::move(qrToken))
{
}

string Utilisateur::nomComplet() const
{
    // Comptes crees avant l'ajout du nom et du prenom : on affiche la licence.
    if (m_prenom.empty() && m_nom.empty()) {
        return "licence " + m_numeroLicence;
    }
    return m_prenom + " " + m_nom;
}

string Utilisateur::contenuQrCode() const
{
    return PREFIXE_QR + m_qrToken;
}

// --------------------------------------------------------- UtilisateurManager

namespace {

const char* COLONNES = "id, numero_licence, nom, prenom, filiere, statut, qr_token";

Utilisateur depuisLigne(const pqxx::row& ligne)
{
    return Utilisateur(ligne["id"].as<int>(),
                       ligne["numero_licence"].as<string>(),
                       ligne["nom"].as<string>(),
                       ligne["prenom"].as<string>(),
                       ligne["filiere"].as<string>(),
                       statutDepuisTexte(ligne["statut"].as<string>()),
                       ligne["qr_token"].as<string>());
}

} // namespace

UtilisateurManager::UtilisateurManager(string connexion) : Manager(std::move(connexion)) {}

optional<Utilisateur> UtilisateurManager::inscrire(const string& numeroLicence, const string& motDePasse,
                                                   const string& nom, const string& prenom,
                                                   const string& filiere)
{
    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);

        // S'il n'existe encore aucun admin, le nouveau compte devient admin
        // (sinon personne ne pourrait creer d'equipe).
        const bool aucunAdmin =
            tx.exec("SELECT COUNT(*) FROM utilisateurs WHERE statut = 'admin';")[0][0].as<int>() == 0;

        // Mot de passe hache (bcrypt) par PostgreSQL : jamais stocke en clair.
        pqxx::result res = db::exec(tx,
            string("INSERT INTO utilisateurs (numero_licence, password, nom, prenom, filiere, statut) "
                   "VALUES ($1, crypt($2, gen_salt('bf')), $3, $4, $5, $6) RETURNING ") + COLONNES + ";",
            numeroLicence, motDePasse, nom, prenom, filiere, string(aucunAdmin ? "admin" : "client"));

        tx.commit();
        return depuisLigne(res[0]);
    } catch (const pqxx::unique_violation&) {
        return nullopt; // numero de licence deja utilise
    }
}

optional<Utilisateur> UtilisateurManager::connecter(const string& numeroLicence, const string& motDePasse)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    // crypt(motDePasseSaisi, hashStocke) redonne le hash stocke si le mot de passe est bon.
    pqxx::result res = db::exec(tx,
        string("SELECT ") + COLONNES + " FROM utilisateurs "
        "WHERE numero_licence = $1 AND password = crypt($2, password);",
        numeroLicence, motDePasse);

    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

vector<Utilisateur> UtilisateurManager::tous()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    vector<Utilisateur> utilisateurs;
    for (const auto& ligne : tx.exec(string("SELECT ") + COLONNES + " FROM utilisateurs ORDER BY nom, prenom;")) {
        utilisateurs.push_back(depuisLigne(ligne));
    }
    return utilisateurs;
}

optional<Utilisateur> UtilisateurManager::parId(int id)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, string("SELECT ") + COLONNES + " FROM utilisateurs WHERE id = $1;", id);
    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

optional<Utilisateur> UtilisateurManager::parContenuQrCode(const string& contenuQr)
{
    const string& prefixe = Utilisateur::PREFIXE_QR;
    if (contenuQr.rfind(prefixe, 0) != 0) {
        return nullopt; // ce n'est pas un QR code de l'AS Volley
    }
    const string token = contenuQr.substr(prefixe.size());

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, string("SELECT ") + COLONNES + " FROM utilisateurs WHERE qr_token = $1;", token);
    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

void UtilisateurManager::changerStatut(const Utilisateur& auteur, int utilisateurId, Statut statut)
{
    exigerAdmin(auteur, "changer le statut d'un membre");
    if (utilisateurId == auteur.getId() && statut != Statut::Admin) {
        throw DonneeInvalide("Vous ne pouvez pas retirer vos propres droits admin.");
    }

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "UPDATE utilisateurs SET statut = $1 WHERE id = $2;", statutVersTexte(statut), utilisateurId);
    tx.commit();
}

void UtilisateurManager::supprimer(const Utilisateur& auteur, int utilisateurId)
{
    exigerAdmin(auteur, "supprimer un compte");
    if (utilisateurId == auteur.getId()) {
        throw DonneeInvalide("Vous ne pouvez pas supprimer votre propre compte.");
    }

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    db::exec(tx, "DELETE FROM utilisateurs WHERE id = $1;", utilisateurId);
    tx.commit();
}
