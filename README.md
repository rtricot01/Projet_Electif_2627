# AS Volley - INSA Strasbourg

Gestion du club (C++17, POO) : membres, equipes, entrainements avec validation des presences
par QR code, et matchs (CFE / ACAD) avec le detail du score.

Deux programmes partagent les memes classes :
- **`site`** : le site web (serveur C++ + pages dans `web/`), pense pour le telephone ;
- **`app`** : le programme console.

## Site web

### Lancer le site sur son ordinateur

1. Creer un fichier `.env` a la racine (il n'est jamais envoye sur GitHub) :
   ```
   DATABASE_URL=postgresql://utilisateur:motdepasse@hote/base?sslmode=require
   ```
2. Compiler (cible `site`), puis dans VS Code lancer la configuration **Lancer le site web**.
3. Ouvrir http://localhost:8080 dans le navigateur.

La camera (scan des QR codes) ne fonctionne qu'en HTTPS ou sur `localhost` : pour tester le scan
avec un telephone, utilisez le site deploye sur Railway, ou la saisie manuelle du code.

### Mettre le site en ligne sur Railway

1. Sur railway.com : *New Project > Deploy from GitHub repo* et choisir ce depot.
   Railway trouve le `Dockerfile` et le fichier `railway.json` tout seul.
2. Dans l'onglet *Variables* du service, ajouter `DATABASE_URL` (adresse Neon, celle avec `-pooler`).
3. Dans *Settings* : choisir une region europeenne (proche de la base Neon a Francfort),
   puis *Networking > Generate Domain* pour obtenir l'adresse https du site.
4. Dans les reglages du compte Railway, fixer une limite de depenses (*Usage limits*).

A chaque `push` sur la branche choisie, Railway recompile et redeploie le site.

### Organisation du site

| Fichier | Role |
|---|---|
| `src/main_site.cpp` | demarrage : lit `DATABASE_URL` et `PORT`, cree les tables, lance le serveur |
| `ServeurWeb` | routes de l'API JSON (`/api/...`), cookies de session, erreurs |
| `Session` | sessions de connexion (jeton secret dans un cookie, empreinte SHA-256 dans la base) |
| `web/index.html`, `web/app.js`, `web/style.css` | pages du site (sans framework) |
| `web/vendor/html5-qrcode.min.js` | lecture des QR codes avec la camera (licence Apache 2.0) |

Securite : mots de passe haches (bcrypt), cookie `HttpOnly` + `SameSite`, en-tete `X-ASV` exige
sur toutes les modifications (protection CSRF), droits admin verifies par les classes `*Manager`.

## Lancer le programme console

1. Definir l'adresse de la base PostgreSQL (Neon) dans la variable `DATABASE_URL`
   (elle n'est **jamais** ecrite dans le code, le depot est public) :
   - PowerShell : `$env:DATABASE_URL="postgresql://utilisateur:motdepasse@hote/base?sslmode=require"`
   - bash : `export DATABASE_URL="postgresql://utilisateur:motdepasse@hote/base?sslmode=require"`
2. Compiler avec CMake (les dependances `libpqxx`, `nayuki-qr-code-generator`, `cpp-httplib` et
   `nlohmann-json` sont installees par vcpkg ; sous Linux : paquets `libpqxx-dev`, `libqrcodegencpp-dev`,
   `libcpp-httplib-dev`, `nlohmann-json3-dev`).
3. Lancer `app`. Les tables sont creees automatiquement au demarrage.

## Comptes des joueurs

1. **Installation** : tant que le club n'a aucun admin, l'accueil propose *Creer le compte administrateur du club*.
2. **Import de la liste** : l'admin enregistre le tableau Excel des joueurs en CSV
   (*Fichier > Enregistrer sous > CSV UTF-8* ou *CSV (separateur : point-virgule)*) puis l'importe via
   *Membres du club > Importer la liste des joueurs*. La premiere ligne doit contenir les titres des
   colonnes : au moins `Nom` et `Licence` (`Prenom` et `Filiere` sont aussi reconnus, les autres colonnes
   sont ignorees). On peut reimporter la liste mise a jour a tout moment : les comptes existants sont gardes.
   Une colonne `Type` (valeurs *Loisir* / *Competition*) indique la pratique du joueur.
   Un joueur qui fait partie d'une equipe est toujours considere comme *competition*.
3. **Premiere connexion** : le joueur saisit son numero de licence et choisit son mot de passe.
4. **Connexions suivantes** : numero de licence + mot de passe, puis inscription aux seances.
5. **Mot de passe oublie** : un admin le reinitialise (*Membres du club*), le joueur en choisit un nouveau.

## Organisation du code

| Classe | Role |
|---|---|
| `Utilisateur` / `UtilisateurManager` | membres : nom, prenom, filiere, numero de licence (identifiant), statut client/admin, QR code personnel, activation du compte |
| `ListeJoueurs` | lecture du fichier CSV exporte d'Excel (liste des joueurs) |
| `Equipe` / `EquipeManager` | equipes : nom, photo (stockee dans la base), liste des joueurs |
| `Entrainement` / `EntrainementManager` | seances : date, lieu, inscriptions, presences validees par QR code |
| `Match` / `MatchManager` | matchs : equipe, adversaire, CFE/ACAD, score set par set |
| `Manager` | classe mere des managers : connexion a la base + verification des droits admin |
| `QrCode` | dessine un QR code dans la console ou dans un fichier SVG |
| `Application` | menus de la console |
| `Init` | creation des tables |

Les regles (droits admin, verifications) sont dans les classes `*Manager`, pas dans les menus :
le futur site web pourra les reutiliser telles quelles.

## Inscriptions aux seances

- Chaque seance a un nombre de places (50 par defaut, modifiable). Quand c'est complet, les joueurs
  ne peuvent plus s'inscrire ; un admin peut toujours ajouter ou retirer des joueurs (page *Inscrits*).
- Type de seance, choisi par l'admin :
  - **Ouverte a tous** ;
  - **Semaine A** : les joueurs loisirs s'inscrivent des la creation, les joueurs competition
    seulement 48 h avant la seance (`Entrainement::HEURES_PRIORITE_LOISIRS`) ;
  - **Semaine B** : uniquement les joueurs competition.
- La base est reglee sur le fuseau `Europe/Paris` au demarrage (Neon est en UTC par defaut).

## Presences par QR code

- Chaque membre a un QR code personnel (menu *Mon profil et mon QR code*), a afficher ou enregistrer en SVG.
- Au debut de l'entrainement, le responsable (admin) ouvre *Entrainements > Scanner les QR codes*
  et scanne le QR code de chaque joueur. Le programme affiche `presents / inscrits`.
- En console, le contenu du QR code est saisi au clavier : une appli de scan sur telephone
  ou une douchette USB peut le "taper". La lecture directe par la camera du telephone
  sera ajoutee avec le site web.
