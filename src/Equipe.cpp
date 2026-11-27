#include "Equipe.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <pqxx/pqxx>
#include <utility>

#include "Db.h"
#include "Utilisateur.h"

using namespace std;

// Type utilise par libpqxx pour les donnees binaires (BYTEA).
using Octets = basic_string<byte>;

// -------------------------------------------------------------------- Equipe

Equipe::Equipe(int id, string nom, vector<int> membres, bool aUnePhoto)
    : m_id(id), m_nom(std::move(nom)), m_membres(std::move(membres)), m_aUnePhoto(aUnePhoto)
{
}

bool Equipe::contient(int utilisateurId) const
{
    return find(m_membres.begin(), m_membres.end(), utilisateurId) != m_membres.end();
}

// -------------------------------------------------------------- EquipeManager

namespace {

void verifierNom(const string& nom)
{
    if (nom.empty()) {
        throw DonneeInvalide("Le nom de l'equipe ne peut pas etre vide.");
    }
}

void enregistrerMembres(pqxx::work& tx, int equipeId, const vector<int>& membres)
{
    db::exec(tx, "DELETE FROM equipe_membres WHERE equipe_id = $1;", equipeId);
    for (int utilisateurId : membres) {
        db::exec(tx,
            "INSERT INTO equipe_membres (equipe_id, utilisateur_id) VALUES ($1, $2) ON CONFLICT DO NOTHING;",
            equipeId, utilisateurId);
    }
}

vector<int> lireMembres(pqxx::work& tx, int equipeId)
{
    vector<int> membres;
    for (const auto& ligne : db::exec(tx,
             "SELECT utilisateur_id FROM equipe_membres WHERE equipe_id = $1 ORDER BY utilisateur_id;", equipeId)) {
        membres.push_back(ligne[0].as<int>());
    }
    return membres;
}

} // namespace

EquipeManager::EquipeManager(string connexion) : Manager(std::move(connexion)) {}

string EquipeManager::typeImage(const string& contenu)
{
    if (contenu.rfind("\xFF\xD8\xFF", 0) == 0) {
        return "image/jpeg";
    }
    if (contenu.rfind("\x89PNG\r\n\x1A\n", 0) == 0) {
        return "image/png";
    }
    if (contenu.size() >= 12 && contenu.compare(0, 4, "RIFF") == 0 && contenu.compare(8, 4, "WEBP") == 0) {
        return "image/webp";
    }
    return "";
}

Equipe EquipeManager::creer(const Utilisateur& auteur, const string& nom, const vector<int>& membres)
{
    exigerAdmin(auteur, "creer une equipe");
    verifierNom(nom);

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    try {
        const int id = db::exec(tx, "INSERT INTO equipes (nom) VALUES ($1) RETURNING id;", nom)[0][0].as<int>();
        enregistrerMembres(tx, id, membres);
        vector<int> membresEnregistres = lireMembres(tx, id);
        tx.commit();
        return Equipe(id, nom, membresEnregistres, false);
    } catch (const pqxx::unique_violation&) {
        throw DonneeInvalide("Une equipe s'appelle deja \"" + nom + "\".");
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Un des joueurs choisis n'existe pas.");
    }
}

void EquipeManager::modifier(const Utilisateur& auteur, int equipeId, const string& nom, const vector<int>& membres)
{
    exigerAdmin(auteur, "modifier une equipe");
    verifierNom(nom);

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    try {
        if (db::exec(tx, "UPDATE equipes SET nom = $1 WHERE id = $2;", nom, equipeId).affected_rows() == 0) {
            throw DonneeInvalide("Cette equipe n'existe pas.");
        }
        enregistrerMembres(tx, equipeId, membres);
        tx.commit();
    } catch (const pqxx::unique_violation&) {
        throw DonneeInvalide("Une equipe s'appelle deja \"" + nom + "\".");
    } catch (const pqxx::foreign_key_violation&) {
        throw DonneeInvalide("Un des joueurs choisis n'existe pas.");
    }
}

void EquipeManager::supprimer(const Utilisateur& auteur, int equipeId)
{
    exigerAdmin(auteur, "supprimer une equipe");

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    // Les membres et les matchs de l'equipe sont supprimes avec elle (ON DELETE CASCADE).
    db::exec(tx, "DELETE FROM equipes WHERE id = $1;", equipeId);
    tx.commit();
}

vector<Equipe> EquipeManager::toutes()
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    vector<Equipe> equipes;
    for (const auto& ligne : tx.exec("SELECT id, nom, photo IS NOT NULL AS a_photo FROM equipes ORDER BY nom;")) {
        const int id = ligne["id"].as<int>();
        equipes.emplace_back(id, ligne["nom"].as<string>(), lireMembres(tx, id), ligne["a_photo"].as<bool>());
    }
    return equipes;
}

optional<Equipe> EquipeManager::parId(int equipeId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx,
        "SELECT id, nom, photo IS NOT NULL AS a_photo FROM equipes WHERE id = $1;", equipeId);
    if (res.empty()) {
        return nullopt;
    }
    return Equipe(equipeId, res[0]["nom"].as<string>(), lireMembres(tx, equipeId), res[0]["a_photo"].as<bool>());
}

void EquipeManager::definirPhoto(const Utilisateur& auteur, int equipeId, const string& cheminFichier)
{
    exigerAdmin(auteur, "changer la photo d'une equipe");

    ifstream fichier(cheminFichier, ios::binary);
    if (!fichier) {
        throw DonneeInvalide("Impossible d'ouvrir le fichier \"" + cheminFichier + "\".");
    }
    definirPhotoDepuisOctets(auteur, equipeId,
                             string((istreambuf_iterator<char>(fichier)), istreambuf_iterator<char>()));
}

void EquipeManager::definirPhotoDepuisOctets(const Utilisateur& auteur, int equipeId, const string& contenu)
{
    exigerAdmin(auteur, "changer la photo d'une equipe");
    if (contenu.empty()) {
        throw DonneeInvalide("Le fichier est vide.");
    }
    if (contenu.size() > TAILLE_MAX_PHOTO) {
        throw DonneeInvalide("La photo est trop lourde (5 Mo maximum).");
    }
    if (typeImage(contenu).empty()) {
        throw DonneeInvalide("Le fichier doit etre une image JPEG, PNG ou WebP.");
    }
    const Octets photo(reinterpret_cast<const byte*>(contenu.data()), contenu.size());

    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);
    if (db::exec(tx, "UPDATE equipes SET photo = $1 WHERE id = $2;", photo, equipeId).affected_rows() == 0) {
        throw DonneeInvalide("Cette equipe n'existe pas.");
    }
    tx.commit();
}

optional<string> EquipeManager::photo(int equipeId)
{
    pqxx::connection conn(m_connexion);
    pqxx::work tx(conn);

    pqxx::result res = db::exec(tx, "SELECT photo FROM equipes WHERE id = $1 AND photo IS NOT NULL;", equipeId);
    if (res.empty()) {
        return nullopt;
    }
    const Octets octets = res[0][0].as<Octets>();
    return string(reinterpret_cast<const char*>(octets.data()), octets.size());
}

bool EquipeManager::exporterPhoto(int equipeId, const string& cheminFichier)
{
    const optional<string> contenu = photo(equipeId);
    if (!contenu) {
        return false;
    }
    ofstream fichier(cheminFichier, ios::binary);
    if (!fichier) {
        throw DonneeInvalide("Impossible d'ecrire le fichier \"" + cheminFichier + "\".");
    }
    fichier.write(contenu->data(), static_cast<streamsize>(contenu->size()));
    return true;
}
