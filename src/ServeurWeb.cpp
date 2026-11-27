#include "ServeurWeb.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <iostream>
#include <map>
#include <utility>

#include "ListeJoueurs.h"
#include "QrCode.h"

using namespace std;
using Json = nlohmann::json;

namespace {

const string NOM_COOKIE = "asv_session";

// Levee quand une route reservee aux membres est appelee sans etre connecte.
struct NonConnecte {};

// ------------------------------------------------------------------ reponses

void envoyerJson(httplib::Response& reponse, const Json& contenu, int statut = 200)
{
    reponse.status = statut;
    reponse.set_content(contenu.dump(), "application/json; charset=utf-8");
}

void envoyerErreur(httplib::Response& reponse, int statut, const string& message)
{
    envoyerJson(reponse, Json{{"erreur", message}}, statut);
}

// ------------------------------------------------------------------ cookies

string lireCookie(const httplib::Request& requete, const string& nom)
{
    const string cookies = requete.get_header_value("Cookie");
    size_t position = 0;
    while (position < cookies.size()) {
        size_t fin = cookies.find(';', position);
        if (fin == string::npos) {
            fin = cookies.size();
        }
        string morceau = cookies.substr(position, fin - position);
        const size_t debut = morceau.find_first_not_of(' ');
        if (debut != string::npos) {
            morceau.erase(0, debut);
        }
        if (morceau.rfind(nom + "=", 0) == 0) {
            return morceau.substr(nom.size() + 1);
        }
        position = fin + 1;
    }
    return "";
}

// Le site est en HTTPS sur Railway : le cookie n'est alors envoye que sur une connexion chiffree.
bool connexionChiffree(const httplib::Request& requete)
{
    return requete.get_header_value("X-Forwarded-Proto") == "https";
}

void ecrireCookieSession(const httplib::Request& requete, httplib::Response& reponse, const string& jeton,
                         int dureeSecondes)
{
    string cookie = NOM_COOKIE + "=" + jeton + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + to_string(dureeSecondes);
    if (connexionChiffree(requete)) {
        cookie += "; Secure";
    }
    reponse.set_header("Set-Cookie", cookie);
}

// ------------------------------------------------------------------ lecture du JSON recu

Json lireCorps(const httplib::Request& requete)
{
    if (requete.body.empty()) {
        return Json::object();
    }
    return Json::parse(requete.body); // leve Json::exception si invalide -> erreur 400
}

string texte(const Json& corps, const string& cle)
{
    if (!corps.contains(cle) || corps[cle].is_null()) {
        return "";
    }
    return corps.at(cle).get<string>();
}

optional<int> entierFacultatif(const Json& corps, const string& cle)
{
    if (!corps.contains(cle) || corps[cle].is_null()) {
        return nullopt;
    }
    return corps.at(cle).get<int>();
}

// ------------------------------------------------------------------ conversion en JSON

Json versJson(const Utilisateur& u)
{
    return {
        {"id", u.getId()},
        {"numeroLicence", u.getNumeroLicence()},
        {"nom", u.getNom()},
        {"prenom", u.getPrenom()},
        {"nomComplet", u.nomComplet()},
        {"filiere", u.getFiliere()},
        {"admin", u.estAdmin()},
        {"compteActive", u.compteActive()},
        {"typeJoueur", typeJoueurVersTexte(u.typeJoueur())},
        {"typeDeclare", typeJoueurVersTexte(u.getTypeDeclare())},
        {"dansUneEquipe", u.estDansUneEquipe()},
    };
}

// Lit les champs d'une seance envoyes par le formulaire.
ParametresSeance lireSeance(const Json& corps)
{
    ParametresSeance parametres;
    parametres.dateHeure = texte(corps, "dateHeure");
    parametres.lieu = texte(corps, "lieu");
    parametres.equipeId = entierFacultatif(corps, "equipeId");
    parametres.description = texte(corps, "description");
    parametres.type = typeSeanceDepuisTexte(texte(corps, "type"));
    parametres.places = entierFacultatif(corps, "places").value_or(50);
    return parametres;
}

string texteResultatScan(ResultatScan resultat)
{
    switch (resultat) {
    case ResultatScan::PresenceValidee: return "valide";
    case ResultatScan::AjouteEtValide: return "ajoute";
    case ResultatScan::DejaPresent: return "deja_present";
    case ResultatScan::QrCodeInconnu: return "inconnu";
    }
    return "inconnu";
}

Json feuilleVersJson(const vector<LignePresence>& feuille)
{
    Json presents = Json::array();
    Json absents = Json::array();
    for (const LignePresence& ligne : feuille) {
        if (ligne.estPresent()) {
            presents.push_back({{"id", ligne.utilisateurId}, {"nom", ligne.nomComplet}, {"heure", ligne.heureScan},
                                {"competition", ligne.competition}});
        } else {
            absents.push_back({{"id", ligne.utilisateurId}, {"nom", ligne.nomComplet},
                               {"competition", ligne.competition}});
        }
    }
    return {{"presents", presents}, {"absents", absents}, {"inscrits", feuille.size()}};
}

vector<ScoreSet> lireSets(const Json& corps)
{
    vector<ScoreSet> sets;
    for (const Json& set : corps.at("sets")) {
        sets.push_back({set.at("equipe").get<int>(), set.at("adversaire").get<int>()});
    }
    return sets;
}

} // namespace

// ====================================================================== Contexte

const Utilisateur& ServeurWeb::Contexte::connecte() const
{
    if (!membre) {
        throw NonConnecte{};
    }
    return *membre;
}

const Utilisateur& ServeurWeb::Contexte::admin() const
{
    const Utilisateur& u = connecte();
    if (!u.estAdmin()) {
        throw AccesRefuse("faire cette action");
    }
    return u;
}

int ServeurWeb::Contexte::parametre(size_t numero) const
{
    return stoi(requete.matches[numero].str());
}

// ====================================================================== ServeurWeb

ServeurWeb::ServeurWeb(const string& connexion, string dossierWeb)
    : m_dossierWeb(std::move(dossierWeb)),
      m_serveur(make_unique<httplib::Server>()),
      m_utilisateurs(connexion),
      m_sessions(connexion),
      m_equipes(connexion),
      m_entrainements(connexion),
      m_matchs(connexion)
{
    // Une photo d'equipe peut faire 5 Mo.
    m_serveur->set_payload_max_length(EquipeManager::TAILLE_MAX_PHOTO + 1024 * 1024);

    // En-tetes de securite ajoutes a toutes les reponses.
    m_serveur->set_post_routing_handler([](const httplib::Request&, httplib::Response& reponse) {
        reponse.set_header("X-Content-Type-Options", "nosniff");
        reponse.set_header("Referrer-Policy", "same-origin");
        reponse.set_header("Permissions-Policy", "camera=(self), microphone=(), geolocation=()");
        reponse.set_header("Content-Security-Policy",
            "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; "
            "style-src 'self' 'unsafe-inline'; frame-ancestors 'none'");
    });

    if (!m_serveur->set_mount_point("/", m_dossierWeb)) {
        cerr << "[SITE] Dossier des pages introuvable : " << m_dossierWeb << '\n';
    }
    declarerRoutes();
}

ServeurWeb::~ServeurWeb() = default;

bool ServeurWeb::demarrer(const string& hote, int port)
{
    cout << "[SITE] En ecoute sur http://" << hote << ':' << port << '\n';
    return m_serveur->listen(hote, port);
}

// Enregistre une route et gere toutes les erreurs au meme endroit.
void ServeurWeb::route(const string& methode, const string& motif, Action action)
{
    const bool modifie = methode != "GET";

    auto gestionnaire = [this, action, modifie](const httplib::Request& requete, httplib::Response& reponse) {
        // Protection CSRF : un autre site ne peut pas ajouter cet en-tete a une requete vers le notre.
        if (modifie && requete.get_header_value("X-ASV") != "1") {
            envoyerErreur(reponse, 403, "Requete refusee.");
            return;
        }
        try {
            Contexte contexte{requete, reponse, nullopt};
            const string jeton = lireCookie(requete, NOM_COOKIE);
            if (!jeton.empty()) {
                contexte.membre = m_sessions.utilisateur(jeton);
            }
            action(contexte);
        } catch (const NonConnecte&) {
            envoyerErreur(reponse, 401, "Vous devez vous connecter.");
        } catch (const AccesRefuse& e) {
            envoyerErreur(reponse, 403, e.what());
        } catch (const DonneeInvalide& e) {
            envoyerErreur(reponse, 400, e.what());
        } catch (const Json::exception&) {
            envoyerErreur(reponse, 400, "Donnees envoyees invalides.");
        } catch (const pqxx::broken_connection& e) {
            cerr << "[SITE] Base injoignable : " << e.what() << '\n';
            envoyerErreur(reponse, 503, "Base de donnees injoignable, reessayez dans un instant.");
        } catch (const exception& e) {
            cerr << "[SITE] Erreur sur " << requete.method << ' ' << requete.path << " : " << e.what() << '\n';
            envoyerErreur(reponse, 500, "Erreur interne du serveur.");
        }
    };

    if (methode == "GET") {
        m_serveur->Get(motif, gestionnaire);
    } else if (methode == "POST") {
        m_serveur->Post(motif, gestionnaire);
    } else if (methode == "PUT") {
        m_serveur->Put(motif, gestionnaire);
    } else if (methode == "DELETE") {
        m_serveur->Delete(motif, gestionnaire);
    }
}

void ServeurWeb::declarerRoutes()
{
    // ------------------------------------------------------------ general

    route("GET", "/api/sante", [](Contexte& c) { envoyerJson(c.reponse, {{"ok", true}}); });

    // ------------------------------------------------------------ connexion

    // Etape 1 : le membre tape sa licence, le site dit s'il faut un mot de passe ou en choisir un.
    route("POST", "/api/connexion/etat", [this](Contexte& c) {
        const string licence = texte(lireCorps(c.requete), "licence");
        string etat = "inconnue";
        switch (m_utilisateurs.etatCompte(licence)) {
        case EtatCompte::LicenceInconnue: etat = "inconnue"; break;
        case EtatCompte::AActiver: etat = "a_activer"; break;
        case EtatCompte::DejaActive: etat = "active"; break;
        }
        envoyerJson(c.reponse, {{"etat", etat}});
    });

    route("POST", "/api/connexion", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        const optional<Utilisateur> membre = m_utilisateurs.connecter(texte(corps, "licence"), texte(corps, "motDePasse"));
        if (!membre) {
            envoyerErreur(c.reponse, 401, "Numero de licence ou mot de passe incorrect.");
            return;
        }
        ecrireCookieSession(c.requete, c.reponse, m_sessions.ouvrir(membre->getId()),
                            SessionManager::DUREE_JOURS * 24 * 3600);
        envoyerJson(c.reponse, versJson(*membre));
    });

    // Premiere connexion : le joueur choisit son mot de passe.
    route("POST", "/api/connexion/activer", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        const optional<Utilisateur> membre =
            m_utilisateurs.activerCompte(texte(corps, "licence"), texte(corps, "motDePasse"));
        if (!membre) {
            envoyerErreur(c.reponse, 400, "Ce compte est deja active ou cette licence est inconnue.");
            return;
        }
        ecrireCookieSession(c.requete, c.reponse, m_sessions.ouvrir(membre->getId()),
                            SessionManager::DUREE_JOURS * 24 * 3600);
        envoyerJson(c.reponse, versJson(*membre));
    });

    route("POST", "/api/deconnexion", [this](Contexte& c) {
        const string jeton = lireCookie(c.requete, NOM_COOKIE);
        if (!jeton.empty()) {
            m_sessions.fermer(jeton);
        }
        ecrireCookieSession(c.requete, c.reponse, "", 0);
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // ------------------------------------------------------------ mon compte

    route("GET", "/api/moi", [](Contexte& c) { envoyerJson(c.reponse, versJson(c.connecte())); });

    route("GET", "/api/moi/qrcode.svg", [](Contexte& c) {
        const string image = QrCode::svg(c.connecte().contenuQrCode());
        if (image.empty()) {
            envoyerErreur(c.reponse, 501, "Le serveur ne sait pas dessiner les QR codes.");
            return;
        }
        c.reponse.set_header("Cache-Control", "private, no-store");
        c.reponse.set_content(image, "image/svg+xml");
    });

    // ------------------------------------------------------------ entrainements

    route("GET", "/api/entrainements", [this](Contexte& c) {
        const Utilisateur& membre = c.connecte();
        const vector<int> mesInscriptions = m_entrainements.inscriptionsDe(membre.getId());
        const map<int, StatistiquesPresence> stats = m_entrainements.statistiquesParEntrainement();
        map<int, string> nomsEquipes;
        for (const Equipe& e : m_equipes.toutes()) {
            nomsEquipes[e.getId()] = e.getNom();
        }

        Json liste = Json::array();
        for (const Entrainement& e : m_entrainements.tous()) {
            const auto s = stats.find(e.getId());
            const int nbInscrits = s != stats.end() ? s->second.inscrits : 0;
            const optional<string> refus = e.raisonRefusInscription(membre, nbInscrits);
            Json ligne = {
                {"type", typeSeanceVersTexte(e.getType())},
                {"places", e.getPlaces()},
                {"ouvertureCompetition", e.getOuvertureCompetition()},
                {"commencee", e.estCommencee()},
                {"refusInscription", refus ? Json(*refus) : Json(nullptr)},
                {"id", e.getId()},
                {"dateHeure", e.getDateHeure()},
                {"lieu", e.getLieu()},
                {"description", e.getDescription()},
                {"equipeId", e.getEquipeId() ? Json(*e.getEquipeId()) : Json(nullptr)},
                {"equipe", e.getEquipeId() ? nomsEquipes[*e.getEquipeId()] : "Toutes equipes"},
                {"inscrits", s != stats.end() ? s->second.inscrits : 0},
                {"presents", s != stats.end() ? s->second.presents : 0},
                {"inscrit", find(mesInscriptions.begin(), mesInscriptions.end(), e.getId()) != mesInscriptions.end()},
            };
            liste.push_back(ligne);
        }
        envoyerJson(c.reponse, liste);
    });

    route("POST", R"(/api/entrainements)", [this](Contexte& c) {
        const Entrainement e = m_entrainements.creer(c.connecte(), lireSeance(lireCorps(c.requete)));
        envoyerJson(c.reponse, {{"id", e.getId()}}, 201);
    });

    route("PUT", R"(/api/entrainements/(\d+))", [this](Contexte& c) {
        m_entrainements.modifier(c.connecte(), c.parametre(1), lireSeance(lireCorps(c.requete)));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("DELETE", R"(/api/entrainements/(\d+))", [this](Contexte& c) {
        m_entrainements.supprimer(c.connecte(), c.parametre(1));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("POST", R"(/api/entrainements/(\d+)/inscription)", [this](Contexte& c) {
        m_entrainements.inscrire(c.parametre(1), c.connecte());
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // Gestion de la liste des inscrits par un admin.
    route("POST", R"(/api/entrainements/(\d+)/inscrits)", [this](Contexte& c) {
        m_entrainements.ajouterInscrit(c.connecte(), c.parametre(1),
                                       lireCorps(c.requete).at("utilisateurId").get<int>());
        envoyerJson(c.reponse, feuilleVersJson(m_entrainements.feuilleDePresence(c.parametre(1))));
    });

    route("DELETE", R"(/api/entrainements/(\d+)/inscrits/(\d+))", [this](Contexte& c) {
        m_entrainements.retirerInscrit(c.connecte(), c.parametre(1), c.parametre(2));
        envoyerJson(c.reponse, feuilleVersJson(m_entrainements.feuilleDePresence(c.parametre(1))));
    });

    route("DELETE", R"(/api/entrainements/(\d+)/inscription)", [this](Contexte& c) {
        m_entrainements.desinscrire(c.parametre(1), c.connecte().getId());
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // Scan d'un QR code par un responsable : renvoie le resultat et la liste des presents a jour.
    route("POST", R"(/api/entrainements/(\d+)/scan)", [this](Contexte& c) {
        const int entrainementId = c.parametre(1);
        optional<Utilisateur> joueur;
        const ResultatScan resultat = m_entrainements.validerPresence(
            c.connecte(), entrainementId, texte(lireCorps(c.requete), "contenu"), joueur);

        Json reponse = feuilleVersJson(m_entrainements.feuilleDePresence(entrainementId));
        reponse["resultat"] = texteResultatScan(resultat);
        reponse["joueur"] = joueur ? Json(joueur->nomComplet()) : Json(nullptr);
        envoyerJson(c.reponse, reponse);
    });

    route("GET", R"(/api/entrainements/(\d+)/presences)", [this](Contexte& c) {
        c.admin();
        envoyerJson(c.reponse, feuilleVersJson(m_entrainements.feuilleDePresence(c.parametre(1))));
    });

    // ------------------------------------------------------------ equipes

    route("GET", "/api/equipes", [this](Contexte& c) {
        c.connecte();
        map<int, string> noms;
        for (const Utilisateur& u : m_utilisateurs.tous()) {
            noms[u.getId()] = u.nomComplet();
        }
        Json liste = Json::array();
        for (const Equipe& e : m_equipes.toutes()) {
            Json joueurs = Json::array();
            for (int id : e.getMembres()) {
                joueurs.push_back({{"id", id}, {"nom", noms[id]}});
            }
            liste.push_back({{"id", e.getId()}, {"nom", e.getNom()}, {"photo", e.aUnePhoto()}, {"joueurs", joueurs}});
        }
        envoyerJson(c.reponse, liste);
    });

    route("POST", "/api/equipes", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        const Equipe e = m_equipes.creer(c.connecte(), texte(corps, "nom"), corps.value("joueurs", vector<int>{}));
        envoyerJson(c.reponse, {{"id", e.getId()}}, 201);
    });

    route("PUT", R"(/api/equipes/(\d+))", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        m_equipes.modifier(c.connecte(), c.parametre(1), texte(corps, "nom"), corps.value("joueurs", vector<int>{}));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("DELETE", R"(/api/equipes/(\d+))", [this](Contexte& c) {
        m_equipes.supprimer(c.connecte(), c.parametre(1));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("GET", R"(/api/equipes/(\d+)/photo)", [this](Contexte& c) {
        c.connecte();
        const optional<string> photo = m_equipes.photo(c.parametre(1));
        if (!photo) {
            envoyerErreur(c.reponse, 404, "Pas de photo.");
            return;
        }
        c.reponse.set_header("Cache-Control", "private, max-age=300");
        c.reponse.set_content(*photo, EquipeManager::typeImage(*photo));
    });

    // Le corps de la requete est directement le contenu de l'image.
    route("PUT", R"(/api/equipes/(\d+)/photo)", [this](Contexte& c) {
        m_equipes.definirPhotoDepuisOctets(c.connecte(), c.parametre(1), c.requete.body);
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // ------------------------------------------------------------ matchs

    route("GET", "/api/matchs", [this](Contexte& c) {
        c.connecte();
        map<int, string> nomsEquipes;
        for (const Equipe& e : m_equipes.toutes()) {
            nomsEquipes[e.getId()] = e.getNom();
        }
        Json liste = Json::array();
        for (const Match& m : m_matchs.tous()) {
            Json sets = Json::array();
            for (const ScoreSet& s : m.getSets()) {
                sets.push_back({{"equipe", s.pointsEquipe}, {"adversaire", s.pointsAdversaire}});
            }
            liste.push_back({
                {"id", m.getId()},
                {"dateHeure", m.getDateHeure()},
                {"lieu", m.getLieu()},
                {"equipeId", m.getEquipeId()},
                {"equipe", nomsEquipes[m.getEquipeId()]},
                {"adversaire", m.getAdversaire()},
                {"competition", competitionVersTexte(m.getCompetition())},
                {"description", m.getDescription()},
                {"sets", sets},
                {"setsGagnes", m.setsGagnes()},
                {"setsPerdus", m.setsPerdus()},
                {"score", m.scoreTexte()},
            });
        }
        envoyerJson(c.reponse, liste);
    });

    route("POST", "/api/matchs", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        const Match m = m_matchs.creer(c.connecte(), texte(corps, "dateHeure"), texte(corps, "lieu"),
                                       corps.at("equipeId").get<int>(), texte(corps, "adversaire"),
                                       competitionDepuisTexte(texte(corps, "competition")), texte(corps, "description"));
        envoyerJson(c.reponse, {{"id", m.getId()}}, 201);
    });

    route("PUT", R"(/api/matchs/(\d+))", [this](Contexte& c) {
        const Json corps = lireCorps(c.requete);
        m_matchs.modifier(c.connecte(), c.parametre(1), texte(corps, "dateHeure"), texte(corps, "lieu"),
                          corps.at("equipeId").get<int>(), texte(corps, "adversaire"),
                          competitionDepuisTexte(texte(corps, "competition")), texte(corps, "description"));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("PUT", R"(/api/matchs/(\d+)/score)", [this](Contexte& c) {
        m_matchs.enregistrerScore(c.connecte(), c.parametre(1), lireSets(lireCorps(c.requete)));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("DELETE", R"(/api/matchs/(\d+))", [this](Contexte& c) {
        m_matchs.supprimer(c.connecte(), c.parametre(1));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // ------------------------------------------------------------ membres (admin)

    route("GET", "/api/membres", [this](Contexte& c) {
        c.admin();
        Json liste = Json::array();
        for (const Utilisateur& u : m_utilisateurs.tous()) {
            liste.push_back(versJson(u));
        }
        envoyerJson(c.reponse, liste);
    });

    // Le corps est le fichier CSV tel quel. Avec ?confirmer=1 l'import est enregistre,
    // sinon on renvoie seulement un apercu.
    route("POST", "/api/membres/import", [this](Contexte& c) {
        const Utilisateur& admin = c.admin();
        const ListeJoueurs::Lecture lecture = ListeJoueurs::lireTexteCsv(c.requete.body);

        Json reponse = {{"total", lecture.joueurs.size()}, {"avertissements", lecture.avertissements}};
        Json apercu = Json::array();
        for (size_t i = 0; i < lecture.joueurs.size() && i < 5; ++i) {
            const JoueurImporte& j = lecture.joueurs[i];
            apercu.push_back({{"licence", j.numeroLicence}, {"nom", j.nom}, {"prenom", j.prenom},
                              {"filiere", j.filiere}, {"type", j.typeJoueur}});
        }
        reponse["apercu"] = apercu;

        if (c.requete.get_param_value("confirmer") == "1") {
            const ResultatImport resultat = m_utilisateurs.importerJoueurs(admin, lecture.joueurs);
            reponse["ajoutes"] = resultat.ajoutes;
            reponse["misAJour"] = resultat.misAJour;
        }
        envoyerJson(c.reponse, reponse);
    });

    route("PUT", R"(/api/membres/(\d+)/statut)", [this](Contexte& c) {
        const bool admin = lireCorps(c.requete).at("admin").get<bool>();
        m_utilisateurs.changerStatut(c.connecte(), c.parametre(1), admin ? Statut::Admin : Statut::Client);
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("PUT", R"(/api/membres/(\d+)/type)", [this](Contexte& c) {
        const string type = texte(lireCorps(c.requete), "type");
        if (type != "loisir" && type != "competition") {
            throw DonneeInvalide("Type attendu : loisir ou competition.");
        }
        m_utilisateurs.changerTypeJoueur(c.connecte(), c.parametre(1), typeJoueurDepuisTexte(type));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("POST", R"(/api/membres/(\d+)/reinitialiser)", [this](Contexte& c) {
        m_utilisateurs.reinitialiserMotDePasse(c.connecte(), c.parametre(1));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    route("DELETE", R"(/api/membres/(\d+))", [this](Contexte& c) {
        m_utilisateurs.supprimer(c.connecte(), c.parametre(1));
        envoyerJson(c.reponse, {{"ok", true}});
    });

    // Adresse /api/... inconnue : erreur JSON plutot qu'une page vide.
    m_serveur->set_error_handler([](const httplib::Request& requete, httplib::Response& reponse) {
        if (requete.path.rfind("/api/", 0) == 0 && reponse.body.empty()) {
            envoyerErreur(reponse, reponse.status, "Adresse inconnue.");
        }
    });
}
