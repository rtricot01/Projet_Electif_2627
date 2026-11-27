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
                         string filiere, Statut statut, string qrToken, bool compteActive)
    : m_id(id),
      m_numeroLicence(std::move(numeroLicence)),
      m_nom(std::move(nom)),
      m_prenom(std::move(prenom)),
      m_filiere(std::move(filiere)),
      m_statut(statut),
      m_qrToken(std::move(qrToken)),
      m_compteActive(compteActive)
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

const char* COLONNES =
    "id, numero_licence, nom, prenom, filiere, statut, qr_token, password IS NOT NULL AS compte_active";

Utilisateur depuisLigne(const pqxx::row& ligne)
{
    return Utilisateur(ligne["id"].as<int>(),
                       ligne["numero_licence"].as<string>(),
                       ligne["nom"].as<string>(),
                       ligne["prenom"].as<string>(),
                       ligne["filiere"].as<string>(),
                       statutDepuisTexte(ligne["statut"].as<string>()),
                       ligne["qr_token"].as<string>(),
                       ligne["compte_active"].as<bool>());
}

void verifierMotDePasse(const string& motDePasse)
{
    if (motDePasse.size() < UtilisateurManager::LONGUEUR_MIN_MOT_DE_PASSE) {
        throw DonneeInvalide("Le mot de passe doit faire au moins "
                             + to_string(UtilisateurManager::LONGUEUR_MIN_MOT_DE_PASSE) + " caracteres.");
    }
}

} // namespace

UtilisateurManager::UtilisateurManager(string connexion) : Manager(std::move(connexion)) {}

bool UtilisateurManager::existeUnAdmin()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    return tx.exec("SELECT COUNT(*) FROM utilisateurs WHERE statut = 'admin';")[0][0].as<int>() > 0;
}

optional<Utilisateur> UtilisateurManager::creerPremierAdmin(const string& numeroLicence, const string& motDePasse,
                                                            const string& nom, const string& prenom,
                                                            const string& filiere)
{
    verifierMotDePasse(motDePasse);
    try {
        pqxx::connection conn(m_connexion);
        pqxx::work tx(conn);

        // Verrou : deux personnes ne peuvent pas creer le premier admin en meme temps.
        tx.exec("LOCK TABLE utilisateurs IN SHARE ROW EXCLUSIVE MODE;");
        if (tx.exec("SELECT COUNT(*) FROM utilisateurs WHERE statut = 'admin';")[0][0].as<int>() > 0) {
            throw DonneeInvalide("Un compte admin existe deja : demandez-lui de vous donner les droits.");
        }

        // Mot de passe hache (bcrypt) par PostgreSQL : jamais stocke en clair.
        pqxx::result res = db::exec(tx,
            string("INSERT INTO utilisateurs (numero_licence, password, nom, prenom, filiere, statut) "
                   "VALUES ($1, crypt($2, gen_salt('bf')), $3, $4, $5, 'admin') RETURNING ") + COLONNES + ";",
            numeroLicence, motDePasse, nom, prenom, filiere);

        tx.commit();
        return depuisLigne(res[0]);
    } catch (const pqxx::unique_violation&) {
        return nullopt; // numero de licence deja utilise
    }
}

ResultatImport UtilisateurManager::importerJoueurs(const Utilisateur& auteur, const vector<JoueurImporte>& joueurs)
{
    exigerAdmin(auteur, "importer la liste des joueurs");

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    // Tout l'import se fait dans une seule transaction : en cas d'erreur, rien n'est enregistre.
    ResultatImport resultat;
    for (const JoueurImporte& joueur : joueurs) {
        // xmax = 0 signifie que la ligne vient d'etre inseree (et pas mise a jour).
        const bool ajoute = db::exec(tx,
            "INSERT INTO utilisateurs (numero_licence, nom, prenom, filiere) VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (numero_licence) DO UPDATE SET "
            "  nom = EXCLUDED.nom, prenom = EXCLUDED.prenom, "
            "  filiere = CASE WHEN EXCLUDED.filiere = '' THEN utilisateurs.filiere ELSE EXCLUDED.filiere END "
            "RETURNING (xmax = 0) AS ajoute;",
            joueur.numeroLicence, joueur.nom, joueur.prenom, joueur.filiere)[0][0].as<bool>();
        ajoute ? ++resultat.ajoutes : ++resultat.misAJour;
    }
    tx.commit();
    return resultat;
}

EtatCompte UtilisateurManager::etatCompte(const string& numeroLicence)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx,
        "SELECT password IS NOT NULL FROM utilisateurs WHERE numero_licence = $1;", numeroLicence);
    if (res.empty()) {
        return EtatCompte::LicenceInconnue;
    }
    return res[0][0].as<bool>() ? EtatCompte::DejaActive : EtatCompte::AActiver;
}

optional<Utilisateur> UtilisateurManager::activerCompte(const string& numeroLicence, const string& motDePasse)
{
    verifierMotDePasse(motDePasse);

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    // "password IS NULL" : impossible de remplacer le mot de passe d'un compte deja active.
    pqxx::result res = db::exec(tx,
        string("UPDATE utilisateurs SET password = crypt($2, gen_salt('bf')) "
               "WHERE numero_licence = $1 AND password IS NULL RETURNING ") + COLONNES + ";",
        numeroLicence, motDePasse);
    tx.commit();

    if (res.empty()) {
        return nullopt;
    }
    return depuisLigne(res[0]);
}

void UtilisateurManager::reinitialiserMotDePasse(const Utilisateur& auteur, int utilisateurId)
{
    exigerAdmin(auteur, "reinitialiser un mot de passe");

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    if (db::exec(tx, "UPDATE utilisateurs SET password = NULL WHERE id = $1;", utilisateurId).affected_rows() == 0) {
        throw DonneeInvalide("Ce membre n'existe pas.");
    }
    // Deconnecte le membre partout (site web).
    db::exec(tx, "DELETE FROM sessions WHERE utilisateur_id = $1;", utilisateurId);
    tx.commit();
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
