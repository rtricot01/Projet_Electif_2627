// AS Volley INSA - pages du site.
// Tout passe par l'API du serveur C++ (/api/...). Aucune bibliotheque externe a part html5-qrcode.
"use strict";

// ====================================================================== outils

const etat = {
  membre: null,          // membre connecte (ou null)
  lecteur: null,         // scanner de QR codes actif
  minuteur: null,        // rafraichissement de la liste des presents
};

const contenu = document.getElementById("contenu");

// Echappe le texte avant de l'inserer dans le HTML (protection contre les injections).
function esc(texte) {
  return String(texte ?? "").replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  })[c]);
}

class ErreurApi extends Error {
  constructor(message, statut) {
    super(message);
    this.statut = statut;
  }
}

// Appel a l'API. "corps" est un objet (envoye en JSON) ou un fichier (envoye tel quel).
async function api(methode, chemin, corps) {
  const options = { method: methode, credentials: "same-origin", headers: { "X-ASV": "1" } };
  if (corps instanceof Blob) {
    options.body = corps;
  } else if (corps !== undefined) {
    options.headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(corps);
  }

  let reponse;
  try {
    reponse = await fetch(chemin, options);
  } catch {
    throw new ErreurApi("Pas de connexion au serveur. Verifiez votre reseau.", 0);
  }
  const donnees = await reponse.json().catch(() => ({}));
  if (!reponse.ok) {
    if (reponse.status === 401 && chemin !== "/api/connexion") {
      etat.membre = null;
      afficherConnexion();
    }
    throw new ErreurApi(donnees.erreur || "Erreur " + reponse.status, reponse.status);
  }
  return donnees;
}

let minuteurNotification = null;
function notifier(message, erreur = false) {
  const zone = document.getElementById("notification");
  zone.textContent = message;
  zone.classList.toggle("erreur-notif", erreur);
  zone.hidden = false;
  clearTimeout(minuteurNotification);
  minuteurNotification = setTimeout(() => { zone.hidden = true; }, 3500);
}

// "2026-12-03 18:30" -> "jeudi 3 décembre · 18:30"
function formaterDate(dateHeure) {
  const date = versDate(dateHeure);
  if (!date) return esc(dateHeure);
  const jour = date.toLocaleDateString("fr-FR", { weekday: "long", day: "numeric", month: "long" });
  const heure = date.toLocaleTimeString("fr-FR", { hour: "2-digit", minute: "2-digit" });
  return `${esc(jour)} · ${esc(heure)}`;
}

function versDate(dateHeure) {
  const date = new Date(String(dateHeure).replace(" ", "T"));
  return isNaN(date) ? null : date;
}

// Format attendu par <input type="datetime-local">
function versChampDate(dateHeure) {
  return dateHeure ? String(dateHeure).replace(" ", "T").slice(0, 16) : "";
}

function estAdmin() {
  return Boolean(etat.membre && etat.membre.admin);
}

function chargement() {
  contenu.innerHTML = `<p class="vide">Chargement…</p>`;
}

function afficherErreurPage(erreur) {
  if (erreur.statut === 401) return;
  contenu.innerHTML = `<p class="erreur">${esc(erreur.message)}</p>`;
}

// ====================================================================== fenetre (formulaires)

// Ouvre une fenetre avec un formulaire. "valider" recoit le FormData et peut lever une erreur,
// affichee dans la fenetre sans la fermer.
function ouvrirFenetre({ titre, html, texteValider = "Enregistrer", valider, apresOuverture }) {
  const fenetre = document.getElementById("fenetre");
  const formulaire = document.getElementById("fenetre-formulaire");
  const erreur = document.getElementById("fenetre-erreur");
  const boutonValider = document.getElementById("fenetre-valider");

  document.getElementById("fenetre-titre").textContent = titre;
  document.getElementById("fenetre-corps").innerHTML = html;
  boutonValider.textContent = texteValider;
  erreur.hidden = true;

  formulaire.onsubmit = async (evenement) => {
    evenement.preventDefault();
    erreur.hidden = true;
    boutonValider.disabled = true;
    try {
      await valider(new FormData(formulaire), formulaire);
      fenetre.close();
    } catch (e) {
      erreur.textContent = e.message;
      erreur.hidden = false;
    } finally {
      boutonValider.disabled = false;
    }
  };
  document.getElementById("fenetre-annuler").onclick = () => fenetre.close();
  fenetre.showModal();
  if (apresOuverture) apresOuverture(formulaire);
}

// ====================================================================== connexion

function afficherConnexion() {
  arreterScan();
  document.getElementById("entete").hidden = true;
  document.getElementById("onglets").hidden = true;

  contenu.innerHTML = `
    <section class="connexion">
      <div class="connexion-logo">
        <img src="icone.svg" alt="" width="72" height="72">
        <h1>AS Volley INSA Strasbourg</h1>
        <p class="meta">Connectez-vous avec votre numéro de licence.</p>
      </div>
      <form class="carte" id="formulaire-connexion">
        <label class="champ">
          <span>Numéro de licence</span>
          <input name="licence" autocomplete="username" required autofocus>
        </label>
        <div id="etape-mot-de-passe"></div>
        <p class="erreur" id="erreur-connexion" hidden></p>
        <button class="bouton large" type="submit" id="bouton-connexion">Continuer</button>
      </form>
    </section>`;

  const formulaire = document.getElementById("formulaire-connexion");
  const etape = document.getElementById("etape-mot-de-passe");
  const erreur = document.getElementById("erreur-connexion");
  const bouton = document.getElementById("bouton-connexion");
  let etapeActuelle = "licence";

  formulaire.licence.addEventListener("input", () => {
    // Si la licence change, on revient a la premiere etape.
    if (etapeActuelle !== "licence") {
      etapeActuelle = "licence";
      etape.innerHTML = "";
      bouton.textContent = "Continuer";
    }
  });

  formulaire.onsubmit = async (evenement) => {
    evenement.preventDefault();
    erreur.hidden = true;
    bouton.disabled = true;
    const licence = formulaire.licence.value.trim();
    try {
      if (etapeActuelle === "licence") {
        const { etat: etatCompte } = await api("POST", "/api/connexion/etat", { licence });
        if (etatCompte === "inconnue") {
          throw new Error("Ce numéro de licence n'est pas dans la liste du club. Vérifiez-le ou contactez un responsable.");
        }
        if (etatCompte === "a_activer") {
          etapeActuelle = "activer";
          etape.innerHTML = `
            <p class="meta">Première connexion : choisissez votre mot de passe (6 caractères minimum).</p>
            <label class="champ"><span>Nouveau mot de passe</span>
              <input type="password" name="motDePasse" autocomplete="new-password" minlength="6" required></label>
            <label class="champ"><span>Confirmez le mot de passe</span>
              <input type="password" name="confirmation" autocomplete="new-password" minlength="6" required></label>`;
          bouton.textContent = "Créer mon mot de passe";
        } else {
          etapeActuelle = "connexion";
          etape.innerHTML = `
            <label class="champ"><span>Mot de passe</span>
              <input type="password" name="motDePasse" autocomplete="current-password" required></label>
            <p class="aide">Mot de passe oublié ? Demandez à un responsable de le réinitialiser.</p>`;
          bouton.textContent = "Se connecter";
        }
        etape.querySelector("input").focus();
        return;
      }

      const motDePasse = formulaire.motDePasse.value;
      if (etapeActuelle === "activer") {
        if (motDePasse !== formulaire.confirmation.value) {
          throw new Error("Les deux mots de passe sont différents.");
        }
        etat.membre = await api("POST", "/api/connexion/activer", { licence, motDePasse });
      } else {
        etat.membre = await api("POST", "/api/connexion", { licence, motDePasse });
      }
      demarrerSession();
    } catch (e) {
      erreur.textContent = e.message;
      erreur.hidden = false;
    } finally {
      bouton.disabled = false;
    }
  };
}

function demarrerSession() {
  document.getElementById("entete").hidden = false;
  document.getElementById("onglets").hidden = false;
  document.getElementById("nom-membre").textContent = etat.membre.nomComplet;
  for (const lien of document.querySelectorAll("[data-admin]")) {
    lien.hidden = !estAdmin();
  }
  if (!location.hash || location.hash === "#/") {
    location.hash = "#/seances";
  } else {
    router();
  }
}

document.getElementById("bouton-deconnexion").addEventListener("click", async () => {
  try { await api("POST", "/api/deconnexion"); } catch { /* deja deconnecte */ }
  etat.membre = null;
  location.hash = "";
  afficherConnexion();
});

// ====================================================================== navigation

const pages = [
  [/^#\/seances$/, pageSeances],
  [/^#\/seances\/(\d+)\/scan$/, (m) => pageScan(Number(m[1]))],
  [/^#\/seances\/(\d+)\/presences$/, (m) => pagePresences(Number(m[1]))],
  [/^#\/matchs$/, pageMatchs],
  [/^#\/equipes$/, pageEquipes],
  [/^#\/qr$/, pageQrCode],
  [/^#\/membres$/, pageMembres],
];

function router() {
  if (!etat.membre) return;
  arreterScan();
  const hash = location.hash || "#/seances";
  const onglet = hash.split("/")[1];
  for (const lien of document.querySelectorAll(".onglets a")) {
    lien.classList.toggle("actif", lien.dataset.onglet === onglet);
  }
  for (const [motif, page] of pages) {
    const correspondance = hash.match(motif);
    if (correspondance) {
      page(correspondance);
      window.scrollTo(0, 0);
      return;
    }
  }
  location.hash = "#/seances";
}

window.addEventListener("hashchange", router);

// ====================================================================== seances

async function pageSeances() {
  chargement();
  let seances;
  try {
    seances = await api("GET", "/api/entrainements");
  } catch (e) {
    return afficherErreurPage(e);
  }

  const maintenant = Date.now();
  const limite = maintenant - 3 * 3600 * 1000; // une seance reste "en cours" 3 h apres son debut
  const aVenir = seances.filter((s) => (versDate(s.dateHeure)?.getTime() ?? 0) >= limite);
  const passees = seances.filter((s) => (versDate(s.dateHeure)?.getTime() ?? 0) < limite).reverse();

  const carte = (s) => {
    const placesRestantes = Math.max(0, s.places - s.inscrits);
    let boutonInscription;
    if (s.inscrit) {
      boutonInscription = s.commencee ? "" :
        `<button class="bouton secondaire petit" data-action="desinscrire" data-id="${s.id}">Me désinscrire</button>`;
    } else if (s.refusInscription) {
      boutonInscription = `<p class="info-refus">${esc(s.refusInscription)}</p>`;
    } else {
      boutonInscription = `<button class="bouton petit" data-action="inscrire" data-id="${s.id}">M'inscrire</button>`;
    }
    return `
    <article class="carte">
      <div class="carte-ligne">
        <div>
          <div class="date-seance">${formaterDate(s.dateHeure)}</div>
          <div class="meta">${esc(s.lieu)} · ${esc(s.equipe)}</div>
        </div>
        ${s.inscrit ? `<span class="badge succes">Inscrit</span>` : ""}
      </div>
      ${badgeSeance(s)}
      ${s.description ? `<p class="meta">${esc(s.description)}</p>` : ""}
      <div class="jauge" aria-label="${s.inscrits} inscrits sur ${s.places} places">
        <div class="jauge-barre"><span style="width:${Math.min(100, Math.round(100 * s.inscrits / s.places))}%"></span></div>
        <span class="meta">${s.inscrits} / ${s.places} · ${placesRestantes ? `${placesRestantes} place${placesRestantes > 1 ? "s" : ""} libre${placesRestantes > 1 ? "s" : ""}` : "complet"}${s.presents ? ` · ${s.presents} présent${s.presents > 1 ? "s" : ""}` : ""}</span>
      </div>
      <div class="carte-actions">
        ${boutonInscription}
        ${estAdmin() ? `
          <a class="bouton petit" href="#/seances/${s.id}/scan">Scanner</a>
          <a class="bouton secondaire petit" href="#/seances/${s.id}/presences">Inscrits</a>
          <button class="bouton secondaire petit" data-action="modifier" data-id="${s.id}">Modifier</button>
          <button class="bouton danger petit" data-action="supprimer" data-id="${s.id}">Supprimer</button>` : ""}
      </div>
    </article>`;
  };

  contenu.innerHTML = `
    <div class="barre-titre">
      <h1>Séances</h1>
      ${estAdmin() ? `<button class="bouton petit" id="nouvelle-seance">+ Nouvelle séance</button>` : ""}
    </div>
    ${aVenir.length ? aVenir.map(carte).join("") : `<p class="vide">Aucune séance prévue pour le moment.</p>`}
    ${passees.length ? `
      <details>
        <summary class="section-titre">Séances passées (${passees.length})</summary>
        ${passees.map(carte).join("")}
      </details>` : ""}`;

  document.getElementById("nouvelle-seance")?.addEventListener("click", () => formulaireSeance());

  contenu.onclick = async (evenement) => {
    const bouton = evenement.target.closest("button[data-action]");
    if (!bouton) return;
    const id = Number(bouton.dataset.id);
    const seance = seances.find((s) => s.id === id);
    try {
      if (bouton.dataset.action === "inscrire") {
        await api("POST", `/api/entrainements/${id}/inscription`);
        notifier("Vous êtes inscrit.");
      } else if (bouton.dataset.action === "desinscrire") {
        await api("DELETE", `/api/entrainements/${id}/inscription`);
        notifier("Vous êtes désinscrit.");
      } else if (bouton.dataset.action === "modifier") {
        return formulaireSeance(seance);
      } else if (bouton.dataset.action === "supprimer") {
        if (!confirm("Supprimer cette séance et ses présences ?")) return;
        await api("DELETE", `/api/entrainements/${id}`);
        notifier("Séance supprimée.");
      }
      pageSeances();
    } catch (e) {
      notifier(e.message, true);
    }
  };
}

function badgeSeance(s) {
  if (s.type === "A") {
    return `<p class="type-seance"><span class="badge acad">Semaine A</span>
      <span class="meta">Priorité loisirs · compétition à partir du ${esc(s.ouvertureCompetition)}</span></p>`;
  }
  if (s.type === "B") {
    return `<p class="type-seance"><span class="badge cfe">Semaine B</span>
      <span class="meta">Réservée aux joueurs compétition</span></p>`;
  }
  return "";
}

function badgeType(competition) {
  return competition ? `<span class="badge cfe">Compét.</span>` : `<span class="badge">Loisir</span>`;
}

async function optionsEquipes(selection, avecAucune) {
  const equipes = await api("GET", "/api/equipes");
  return (avecAucune ? `<option value="">Toutes équipes</option>` : "") + equipes.map((e) =>
    `<option value="${e.id}" ${e.id === selection ? "selected" : ""}>${esc(e.nom)}</option>`).join("");
}

async function formulaireSeance(seance) {
  let options;
  try {
    options = await optionsEquipes(seance?.equipeId ?? null, true);
  } catch (e) {
    return notifier(e.message, true);
  }
  ouvrirFenetre({
    titre: seance ? "Modifier la séance" : "Nouvelle séance",
    html: `
      <label class="champ"><span>Date et heure</span>
        <input type="datetime-local" name="dateHeure" required value="${esc(versChampDate(seance?.dateHeure))}"></label>
      <label class="champ"><span>Lieu</span>
        <input name="lieu" required value="${esc(seance?.lieu)}" placeholder="Gymnase INSA"></label>
      <label class="champ"><span>Type de séance</span>
        <select name="type">
          <option value="ouverte" ${!seance || seance.type === "ouverte" ? "selected" : ""}>Ouverte à tous</option>
          <option value="A" ${seance?.type === "A" ? "selected" : ""}>Semaine A : priorité aux loisirs</option>
          <option value="B" ${seance?.type === "B" ? "selected" : ""}>Semaine B : compétition uniquement</option>
        </select>
        <div class="aide">Semaine A : les joueurs compétition peuvent s'inscrire 48 h avant la séance.</div></label>
      <label class="champ"><span>Nombre de places</span>
        <input type="number" name="places" min="1" max="500" required value="${seance?.places ?? 50}" inputmode="numeric"></label>
      <label class="champ"><span>Équipe</span>
        <select name="equipeId">${options}</select>
        ${seance ? "" : `<div class="aide">Les joueurs de l'équipe choisie sont inscrits automatiquement.</div>`}</label>
      <label class="champ"><span>Description (facultatif)</span>
        <textarea name="description">${esc(seance?.description)}</textarea></label>`,
    valider: async (donnees) => {
      const corps = {
        dateHeure: donnees.get("dateHeure"),
        lieu: donnees.get("lieu").trim(),
        equipeId: donnees.get("equipeId") ? Number(donnees.get("equipeId")) : null,
        description: donnees.get("description").trim(),
        type: donnees.get("type"),
        places: Number(donnees.get("places")),
      };
      if (seance) {
        await api("PUT", `/api/entrainements/${seance.id}`, corps);
        notifier("Séance modifiée.");
      } else {
        await api("POST", "/api/entrainements", corps);
        notifier("Séance créée.");
      }
      pageSeances();
    },
  });
}

// ====================================================================== presences et scan

function htmlFeuille(feuille) {
  const presents = feuille.presents.length
    ? `<ol class="liste-noms">${feuille.presents.map((p) =>
        `<li><span>${esc(p.nom)}</span><span class="heure">${esc(p.heure)}</span></li>`).join("")}</ol>`
    : `<p class="vide">Personne n'a encore été scanné.</p>`;
  const absents = feuille.absents.length
    ? `<h3 class="section-titre">Pas encore scannés (${feuille.absents.length})</h3>
       <ul class="liste-noms">${feuille.absents.map((p) => `<li><span>${esc(p.nom)}</span></li>`).join("")}</ul>`
    : "";
  return `
    <div class="carte">
      <div class="meta">Présents</div>
      <div class="compteur">${feuille.presents.length} / ${feuille.inscrits}</div>
    </div>
    <div class="carte">${presents}${absents}</div>`;
}

async function pagePresences(id) {
  chargement();
  let feuille;
  try {
    feuille = await api("GET", `/api/entrainements/${id}/presences`);
  } catch (e) {
    return afficherErreurPage(e);
  }
  const ligne = (p) => `
    <li>
      <span>${p.heure ? "✓ " : ""}${esc(p.nom)} ${badgeType(p.competition)}</span>
      <span>${p.heure ? `<span class="heure">${esc(p.heure)}</span>` : ""}
        <button class="bouton danger petit" data-retirer="${p.id}" aria-label="Retirer ${esc(p.nom)}">Retirer</button></span>
    </li>`;

  contenu.innerHTML = `
    <div class="barre-titre">
      <h1>Inscrits</h1>
      <a class="bouton petit" href="#/seances/${id}/scan">Scanner</a>
    </div>
    <div class="carte">
      <div class="carte-ligne">
        <div><div class="meta">Inscrits · présents</div>
          <div class="compteur">${feuille.inscrits} · ${feuille.presents.length}</div></div>
        <button class="bouton petit" id="ajouter-inscrit">+ Ajouter un joueur</button>
      </div>
    </div>
    <div class="carte">
      ${feuille.inscrits ? `<ul class="liste-noms">${[...feuille.presents, ...feuille.absents].map(ligne).join("")}</ul>`
                         : `<p class="vide">Personne n'est inscrit.</p>`}
    </div>
    <p><a href="#/seances">← Retour aux séances</a></p>`;

  contenu.onclick = async (evenement) => {
    const bouton = evenement.target.closest("button[data-retirer]");
    if (!bouton) return;
    const joueur = [...feuille.presents, ...feuille.absents].find((p) => p.id === Number(bouton.dataset.retirer));
    if (!confirm(`Retirer ${joueur.nom} de la séance ?`)) return;
    try {
      await api("DELETE", `/api/entrainements/${id}/inscrits/${joueur.id}`);
      notifier(`${joueur.nom} a été retiré.`);
      pagePresences(id);
    } catch (e) {
      notifier(e.message, true);
    }
  };

  document.getElementById("ajouter-inscrit").addEventListener("click", async () => {
    let membres;
    try {
      membres = await api("GET", "/api/membres");
    } catch (e) {
      return notifier(e.message, true);
    }
    const dejaInscrits = new Set([...feuille.presents, ...feuille.absents].map((p) => p.id));
    const disponibles = membres.filter((m) => !dejaInscrits.has(m.id));
    ouvrirFenetre({
      titre: "Ajouter des joueurs",
      texteValider: "Ajouter",
      html: `
        <p class="aide">Un admin peut ajouter un joueur même si la séance est complète ou réservée.</p>
        <input type="search" id="recherche-ajout" placeholder="Rechercher un joueur" aria-label="Rechercher un joueur" style="margin-bottom:8px;width:100%">
        <div class="cases">${disponibles.map((m) => `
          <label data-nom="${esc((m.nomComplet + " " + m.numeroLicence).toLowerCase())}">
            <input type="checkbox" name="joueurs" value="${m.id}">
            <span>${esc(m.nomComplet)} ${badgeType(m.typeJoueur === "competition")}</span>
          </label>`).join("") || `<p class="meta">Tous les membres sont déjà inscrits.</p>`}</div>`,
      apresOuverture: (formulaire) => {
        formulaire.querySelector("#recherche-ajout").addEventListener("input", (evenement) => {
          const recherche = evenement.target.value.toLowerCase().trim();
          for (const ligneCase of formulaire.querySelectorAll(".cases label")) {
            ligneCase.hidden = recherche !== "" && !ligneCase.dataset.nom.includes(recherche);
          }
        });
      },
      valider: async (donnees) => {
        const ids = donnees.getAll("joueurs").map(Number);
        if (!ids.length) throw new Error("Cochez au moins un joueur.");
        for (const utilisateurId of ids) {
          await api("POST", `/api/entrainements/${id}/inscrits`, { utilisateurId });
        }
        notifier(`${ids.length} joueur${ids.length > 1 ? "s" : ""} ajouté${ids.length > 1 ? "s" : ""}.`);
        pagePresences(id);
      },
    });
  });
}

function arreterScan() {
  clearInterval(etat.minuteur);
  etat.minuteur = null;
  if (etat.lecteur) {
    const lecteur = etat.lecteur;
    etat.lecteur = null;
    lecteur.stop().catch(() => {}).finally(() => lecteur.clear());
  }
}

async function pageScan(id) {
  contenu.innerHTML = `
    <div class="barre-titre">
      <h1>Scan des présences</h1>
      <a class="bouton secondaire petit" href="#/seances">Terminer</a>
    </div>
    <div id="lecteur" class="lecteur"></div>
    <p class="aide" id="aide-camera">Visez le QR code du joueur avec la caméra arrière.</p>
    <div id="resultat-scan" class="resultat-scan" hidden></div>
    <form class="saisie-manuelle" id="saisie-manuelle">
      <input name="contenu" placeholder="Ou collez le contenu du QR code" autocomplete="off" aria-label="Contenu du QR code">
      <button class="bouton secondaire" type="submit">Valider</button>
    </form>
    <div id="feuille" style="margin-top:12px"></div>`;

  const zoneResultat = document.getElementById("resultat-scan");
  const zoneFeuille = document.getElementById("feuille");
  let dernierCode = "";
  let dernierInstant = 0;
  let enCours = false;

  const afficherFeuille = (feuille) => { zoneFeuille.innerHTML = htmlFeuille(feuille); };

  async function envoyer(code) {
    // La camera lit le meme QR code plusieurs fois par seconde : on ignore les repetitions.
    const maintenant = Date.now();
    if (enCours || (code === dernierCode && maintenant - dernierInstant < 3000)) return;
    dernierCode = code;
    dernierInstant = maintenant;
    enCours = true;
    try {
      const reponse = await api("POST", `/api/entrainements/${id}/scan`, { contenu: code });
      const messages = {
        valide: [`✓ ${reponse.joueur} est présent`, "valide"],
        ajoute: [`✓ ${reponse.joueur} ajouté et présent (n'était pas inscrit)`, "valide"],
        deja_present: [`Déjà scanné : ${reponse.joueur}`, "deja"],
        inconnu: ["QR code inconnu", "inconnu"],
      };
      const [message, classe] = messages[reponse.resultat] || messages.inconnu;
      zoneResultat.textContent = message;
      zoneResultat.className = "resultat-scan " + classe;
      zoneResultat.hidden = false;
      if (classe === "valide" && navigator.vibrate) navigator.vibrate(120);
      afficherFeuille(reponse);
    } catch (e) {
      zoneResultat.textContent = e.message;
      zoneResultat.className = "resultat-scan inconnu";
      zoneResultat.hidden = false;
    } finally {
      enCours = false;
    }
  }

  document.getElementById("saisie-manuelle").onsubmit = (evenement) => {
    evenement.preventDefault();
    const champ = evenement.target.contenu;
    const code = champ.value.trim();
    if (code) {
      dernierCode = "";
      envoyer(code);
    }
    champ.value = "";
  };

  // Liste des presents, rafraichie toutes les 4 secondes (pour voir les scans des autres responsables).
  const rafraichir = async () => {
    try {
      afficherFeuille(await api("GET", `/api/entrainements/${id}/presences`));
    } catch (e) {
      if (e.statut !== 401) zoneFeuille.innerHTML = `<p class="erreur">${esc(e.message)}</p>`;
    }
  };
  await rafraichir();
  etat.minuteur = setInterval(rafraichir, 4000);

  // Camera
  const aide = document.getElementById("aide-camera");
  const zoneLecteur = document.getElementById("lecteur");
  if (typeof Html5Qrcode === "undefined") {
    zoneLecteur.hidden = true;
    aide.textContent = "Le lecteur de QR codes n'a pas pu être chargé. Utilisez la saisie manuelle.";
    return;
  }
  if (!window.isSecureContext) {
    zoneLecteur.hidden = true;
    aide.textContent = "La caméra n'est disponible qu'en HTTPS. Utilisez la saisie manuelle.";
    return;
  }
  const lecteur = new Html5Qrcode("lecteur", { verbose: false });
  etat.lecteur = lecteur;
  try {
    await lecteur.start(
      { facingMode: "environment" },
      { fps: 10, qrbox: (largeur, hauteur) => {
          const cote = Math.floor(Math.min(largeur, hauteur) * 0.7);
          return { width: cote, height: cote };
        } },
      (texte) => envoyer(texte),
      () => {} // aucune image reconnue : normal, on continue
    );
  } catch (e) {
    zoneLecteur.hidden = true;
    aide.textContent = "Impossible d'ouvrir la caméra (autorisation refusée ?). Utilisez la saisie manuelle.";
    if (etat.lecteur === lecteur) etat.lecteur = null;
  }
}

// ====================================================================== matchs

async function pageMatchs() {
  chargement();
  let matchs;
  try {
    matchs = await api("GET", "/api/matchs");
  } catch (e) {
    return afficherErreurPage(e);
  }
  matchs.reverse(); // les plus recents d'abord

  const carte = (m) => {
    const joue = m.sets.length > 0;
    const victoire = m.setsGagnes > m.setsPerdus;
    return `
      <article class="carte">
        <div class="carte-ligne">
          <div class="date-seance">${formaterDate(m.dateHeure)}</div>
          <span class="badge ${m.competition === "ACAD" ? "acad" : "cfe"}">${esc(m.competition)}</span>
        </div>
        <h3>${esc(m.equipe)} – ${esc(m.adversaire)}</h3>
        <div class="meta">${esc(m.lieu)}</div>
        ${joue ? `
          <p><strong>${m.setsGagnes} – ${m.setsPerdus}</strong>
            <span class="badge ${victoire ? "succes" : ""}">${victoire ? "Victoire" : "Défaite"}</span></p>
          <div class="meta">${m.sets.map((s) => `${s.equipe}-${s.adversaire}`).join(" · ")}</div>`
          : `<p class="meta">À jouer</p>`}
        ${m.description ? `<p class="meta">${esc(m.description)}</p>` : ""}
        ${estAdmin() ? `
          <div class="carte-actions">
            <button class="bouton petit" data-action="score" data-id="${m.id}">Score</button>
            <button class="bouton secondaire petit" data-action="modifier" data-id="${m.id}">Modifier</button>
            <button class="bouton danger petit" data-action="supprimer" data-id="${m.id}">Supprimer</button>
          </div>` : ""}
      </article>`;
  };

  contenu.innerHTML = `
    <div class="barre-titre">
      <h1>Matchs</h1>
      ${estAdmin() ? `<button class="bouton petit" id="nouveau-match">+ Nouveau match</button>` : ""}
    </div>
    ${matchs.length ? matchs.map(carte).join("") : `<p class="vide">Aucun match pour le moment.</p>`}`;

  document.getElementById("nouveau-match")?.addEventListener("click", () => formulaireMatch());
  contenu.onclick = async (evenement) => {
    const bouton = evenement.target.closest("button[data-action]");
    if (!bouton) return;
    const match = matchs.find((m) => m.id === Number(bouton.dataset.id));
    if (bouton.dataset.action === "modifier") return formulaireMatch(match);
    if (bouton.dataset.action === "score") return formulaireScore(match);
    if (bouton.dataset.action === "supprimer" && confirm("Supprimer ce match ?")) {
      try {
        await api("DELETE", `/api/matchs/${match.id}`);
        notifier("Match supprimé.");
        pageMatchs();
      } catch (e) {
        notifier(e.message, true);
      }
    }
  };
}

async function formulaireMatch(match) {
  let options;
  try {
    options = await optionsEquipes(match?.equipeId ?? null, false);
  } catch (e) {
    return notifier(e.message, true);
  }
  if (!options) {
    return notifier("Créez d'abord une équipe (onglet Équipes).", true);
  }
  ouvrirFenetre({
    titre: match ? "Modifier le match" : "Nouveau match",
    html: `
      <label class="champ"><span>Notre équipe</span><select name="equipeId" required>${options}</select></label>
      <label class="champ"><span>Adversaire</span>
        <input name="adversaire" required value="${esc(match?.adversaire)}"></label>
      <label class="champ"><span>Compétition</span>
        <select name="competition">
          <option value="CFE" ${match?.competition !== "ACAD" ? "selected" : ""}>CFE</option>
          <option value="ACAD" ${match?.competition === "ACAD" ? "selected" : ""}>ACAD</option>
        </select></label>
      <label class="champ"><span>Date et heure</span>
        <input type="datetime-local" name="dateHeure" required value="${esc(versChampDate(match?.dateHeure))}"></label>
      <label class="champ"><span>Lieu</span><input name="lieu" required value="${esc(match?.lieu)}"></label>
      <label class="champ"><span>Description (facultatif)</span>
        <textarea name="description">${esc(match?.description)}</textarea></label>`,
    valider: async (donnees) => {
      const corps = {
        equipeId: Number(donnees.get("equipeId")),
        adversaire: donnees.get("adversaire").trim(),
        competition: donnees.get("competition"),
        dateHeure: donnees.get("dateHeure"),
        lieu: donnees.get("lieu").trim(),
        description: donnees.get("description").trim(),
      };
      if (match) {
        await api("PUT", `/api/matchs/${match.id}`, corps);
        notifier("Match modifié.");
      } else {
        await api("POST", "/api/matchs", corps);
        notifier("Match créé.");
      }
      pageMatchs();
    },
  });
}

function formulaireScore(match) {
  const lignes = [1, 2, 3, 4, 5].map((n) => {
    const set = match.sets[n - 1];
    return `
      <span>Set ${n}</span>
      <input type="number" min="0" max="99" inputmode="numeric" name="e${n}" value="${set ? set.equipe : ""}" aria-label="Set ${n} ${esc(match.equipe)}">
      <span>–</span>
      <input type="number" min="0" max="99" inputmode="numeric" name="a${n}" value="${set ? set.adversaire : ""}" aria-label="Set ${n} ${esc(match.adversaire)}">`;
  }).join("");
  ouvrirFenetre({
    titre: `Score : ${match.equipe} – ${match.adversaire}`,
    html: `
      <p class="aide">Points de ${esc(match.equipe)} à gauche, de ${esc(match.adversaire)} à droite. Laissez vides les sets non joués.</p>
      <div class="sets">${lignes}</div>`,
    valider: async (donnees) => {
      const sets = [];
      for (let n = 1; n <= 5; n++) {
        const e = donnees.get("e" + n);
        const a = donnees.get("a" + n);
        if (e === "" && a === "") continue;
        if (e === "" || a === "") throw new Error(`Set ${n} : il manque un score.`);
        sets.push({ equipe: Number(e), adversaire: Number(a) });
      }
      await api("PUT", `/api/matchs/${match.id}/score`, { sets });
      notifier("Score enregistré.");
      pageMatchs();
    },
  });
}

// ====================================================================== equipes

async function pageEquipes() {
  chargement();
  let equipes;
  try {
    equipes = await api("GET", "/api/equipes");
  } catch (e) {
    return afficherErreurPage(e);
  }

  const carte = (e) => `
    <article class="carte">
      ${e.photo ? `<img class="photo-equipe" src="/api/equipes/${e.id}/photo?v=${Date.now()}" alt="Photo de l'équipe ${esc(e.nom)}">` : ""}
      <h2>${esc(e.nom)}</h2>
      <div class="meta">${e.joueurs.length} joueur${e.joueurs.length > 1 ? "s" : ""}</div>
      ${e.joueurs.length ? `<ul class="liste-noms">${e.joueurs.map((j) => `<li>${esc(j.nom)}</li>`).join("")}</ul>` : ""}
      ${estAdmin() ? `
        <div class="carte-actions">
          <button class="bouton secondaire petit" data-action="modifier" data-id="${e.id}">Modifier</button>
          <label class="bouton secondaire petit">Photo
            <input type="file" accept="image/jpeg,image/png,image/webp" data-photo="${e.id}" hidden></label>
          <button class="bouton danger petit" data-action="supprimer" data-id="${e.id}">Supprimer</button>
        </div>` : ""}
    </article>`;

  contenu.innerHTML = `
    <div class="barre-titre">
      <h1>Équipes</h1>
      ${estAdmin() ? `<button class="bouton petit" id="nouvelle-equipe">+ Nouvelle équipe</button>` : ""}
    </div>
    ${equipes.length ? equipes.map(carte).join("") : `<p class="vide">Aucune équipe pour le moment.</p>`}`;

  document.getElementById("nouvelle-equipe")?.addEventListener("click", () => formulaireEquipe());

  contenu.onclick = async (evenement) => {
    const bouton = evenement.target.closest("button[data-action]");
    if (!bouton) return;
    const equipe = equipes.find((e) => e.id === Number(bouton.dataset.id));
    if (bouton.dataset.action === "modifier") return formulaireEquipe(equipe);
    if (bouton.dataset.action === "supprimer"
        && confirm(`Supprimer l'équipe « ${equipe.nom} » et tous ses matchs ?`)) {
      try {
        await api("DELETE", `/api/equipes/${equipe.id}`);
        notifier("Équipe supprimée.");
        pageEquipes();
      } catch (e) {
        notifier(e.message, true);
      }
    }
  };

  contenu.onchange = async (evenement) => {
    const champ = evenement.target.closest("input[data-photo]");
    if (!champ || !champ.files.length) return;
    const fichier = champ.files[0];
    if (fichier.size > 5 * 1024 * 1024) {
      return notifier("La photo est trop lourde (5 Mo maximum).", true);
    }
    try {
      await api("PUT", `/api/equipes/${champ.dataset.photo}/photo`, fichier);
      notifier("Photo enregistrée.");
      pageEquipes();
    } catch (e) {
      notifier(e.message, true);
    }
  };
}

async function formulaireEquipe(equipe) {
  let membres;
  try {
    membres = await api("GET", "/api/membres");
  } catch (e) {
    return notifier(e.message, true);
  }
  const choisis = new Set((equipe?.joueurs || []).map((j) => j.id));
  const cases = membres.map((m) => `
    <label data-nom="${esc((m.nomComplet + " " + m.numeroLicence).toLowerCase())}">
      <input type="checkbox" name="joueurs" value="${m.id}" ${choisis.has(m.id) ? "checked" : ""}>
      <span>${esc(m.nomComplet)} <span class="meta">${esc(m.filiere)}</span></span>
    </label>`).join("");

  ouvrirFenetre({
    titre: equipe ? "Modifier l'équipe" : "Nouvelle équipe",
    html: `
      <label class="champ"><span>Nom de l'équipe</span>
        <input name="nom" required value="${esc(equipe?.nom)}"></label>
      <div class="champ"><span>Joueurs</span>
        <input type="search" id="recherche-joueur" placeholder="Rechercher un joueur" aria-label="Rechercher un joueur" style="margin-bottom:8px">
        <div class="cases">${cases || `<p class="meta">Aucun membre. Importez d'abord la liste des joueurs.</p>`}</div>
      </div>`,
    apresOuverture: (formulaire) => {
      formulaire.querySelector("#recherche-joueur").addEventListener("input", (evenement) => {
        const recherche = evenement.target.value.toLowerCase().trim();
        for (const ligne of formulaire.querySelectorAll(".cases label")) {
          ligne.hidden = recherche !== "" && !ligne.dataset.nom.includes(recherche);
        }
      });
    },
    valider: async (donnees) => {
      const corps = { nom: donnees.get("nom").trim(), joueurs: donnees.getAll("joueurs").map(Number) };
      if (equipe) {
        await api("PUT", `/api/equipes/${equipe.id}`, corps);
        notifier("Équipe modifiée.");
      } else {
        await api("POST", "/api/equipes", corps);
        notifier("Équipe créée.");
      }
      pageEquipes();
    },
  });
}

// ====================================================================== mon QR code

function pageQrCode() {
  const m = etat.membre;
  contenu.innerHTML = `
    <h1>Mon QR code</h1>
    <div class="carte qr-carte">
      <img src="/api/moi/qrcode.svg" alt="QR code de ${esc(m.nomComplet)}" width="320" height="320">
      <h2 style="margin-top:12px">${esc(m.nomComplet)}</h2>
      <p class="meta">Licence ${esc(m.numeroLicence)}${m.filiere ? " · " + esc(m.filiere) : ""}</p>
      <p class="aide">Montrez ce QR code au responsable au début de chaque séance.
        Pensez à monter la luminosité de l'écran.</p>
      <a class="bouton secondaire" href="/api/moi/qrcode.svg" download="qrcode-${esc(m.numeroLicence)}.svg">Télécharger</a>
    </div>`;
}

// ====================================================================== membres (admin)

async function pageMembres() {
  if (!estAdmin()) {
    location.hash = "#/seances";
    return;
  }
  chargement();
  let membres;
  try {
    membres = await api("GET", "/api/membres");
  } catch (e) {
    return afficherErreurPage(e);
  }
  const nonActives = membres.filter((m) => !m.compteActive).length;

  contenu.innerHTML = `
    <h1>Membres</h1>
    <section class="carte">
      <h2>Importer la liste des joueurs</h2>
      <p class="meta">Dans Excel : Fichier › Enregistrer sous › « CSV UTF-8 » (ou « CSV séparateur point-virgule »).
        La première ligne doit contenir au moins les colonnes <strong>Nom</strong> et <strong>Licence</strong>
        (Prénom, Filière et <strong>Type</strong> — loisir ou compétition — sont aussi reconnus).</p>
      <label class="bouton secondaire">Choisir le fichier CSV
        <input type="file" id="fichier-csv" accept=".csv,text/csv" hidden></label>
      <div id="apercu-import"></div>
    </section>

    <div class="barre-titre" style="margin-top:20px">
      <h2 style="margin:0">${membres.length} membre${membres.length > 1 ? "s" : ""}</h2>
      <span class="meta">${nonActives} compte${nonActives > 1 ? "s" : ""} pas encore activé${nonActives > 1 ? "s" : ""}</span>
    </div>
    <label class="champ"><input type="search" id="recherche-membre" placeholder="Rechercher (nom, licence, filière)" aria-label="Rechercher un membre"></label>
    <div id="liste-membres">
      ${membres.map((m) => `
        <article class="carte" data-recherche="${esc((m.nomComplet + " " + m.numeroLicence + " " + m.filiere).toLowerCase())}">
          <div class="carte-ligne">
            <div>
              <h3>${esc(m.nomComplet)}</h3>
              <div class="meta">Licence ${esc(m.numeroLicence)}${m.filiere ? " · " + esc(m.filiere) : ""}</div>
              ${m.dansUneEquipe && m.typeDeclare === "loisir" ? `<div class="meta">Compétition car membre d'une équipe</div>` : ""}
            </div>
            <div class="badges">
              ${badgeType(m.typeJoueur === "competition")}
              ${m.admin ? `<span class="badge acad">Admin</span>` : ""}
              ${m.compteActive ? "" : `<span class="badge">Pas activé</span>`}
            </div>
          </div>
          <div class="carte-actions">
            <button class="bouton secondaire petit" data-action="type" data-id="${m.id}">
              ${m.typeDeclare === "competition" ? "Passer loisir" : "Passer compétition"}</button>
          ${m.id === etat.membre.id ? "</div>" : `
              <button class="bouton secondaire petit" data-action="statut" data-id="${m.id}" data-admin="${m.admin}">
                ${m.admin ? "Retirer admin" : "Passer admin"}</button>
              ${m.compteActive ? `<button class="bouton secondaire petit" data-action="reinitialiser" data-id="${m.id}">Réinitialiser le mot de passe</button>` : ""}
              <button class="bouton danger petit" data-action="supprimer" data-id="${m.id}">Supprimer</button>
            </div>`}
        </article>`).join("")}
    </div>`;

  document.getElementById("recherche-membre").addEventListener("input", (evenement) => {
    const recherche = evenement.target.value.toLowerCase().trim();
    for (const carte of document.querySelectorAll("#liste-membres .carte")) {
      carte.hidden = recherche !== "" && !carte.dataset.recherche.includes(recherche);
    }
  });

  document.getElementById("fichier-csv").addEventListener("change", async (evenement) => {
    const fichier = evenement.target.files[0];
    if (!fichier) return;
    const zone = document.getElementById("apercu-import");
    try {
      const apercu = await api("POST", "/api/membres/import", fichier);
      zone.innerHTML = `
        <div style="margin-top:12px">
          ${apercu.avertissements.map((a) => `<p class="meta">⚠ ${esc(a)}</p>`).join("")}
          <p><strong>${apercu.total} joueur${apercu.total > 1 ? "s" : ""} trouvé${apercu.total > 1 ? "s" : ""}</strong>, par exemple :</p>
          <ul class="liste-noms">${apercu.apercu.map((j) =>
            `<li><span>${esc(j.prenom)} ${esc(j.nom)}</span><span class="meta">${esc(j.licence)}${j.filiere ? " · " + esc(j.filiere) : ""}${j.type ? " · " + (j.type === "competition" ? "compétition" : "loisir") : ""}</span></li>`).join("")}</ul>
          ${apercu.total ? `<button class="bouton" id="confirmer-import" style="margin-top:8px">Importer ces ${apercu.total} joueurs</button>` : ""}
        </div>`;
      document.getElementById("confirmer-import")?.addEventListener("click", async () => {
        try {
          const resultat = await api("POST", "/api/membres/import?confirmer=1", fichier);
          notifier(`${resultat.ajoutes} ajouté(s), ${resultat.misAJour} mis à jour.`);
          pageMembres();
        } catch (e) {
          notifier(e.message, true);
        }
      });
    } catch (e) {
      zone.innerHTML = `<p class="erreur" style="margin-top:12px">${esc(e.message)}</p>`;
    } finally {
      evenement.target.value = "";
    }
  });

  document.getElementById("liste-membres").onclick = async (evenement) => {
    const bouton = evenement.target.closest("button[data-action]");
    if (!bouton) return;
    const membre = membres.find((m) => m.id === Number(bouton.dataset.id));
    try {
      if (bouton.dataset.action === "type") {
        const nouveau = membre.typeDeclare === "competition" ? "loisir" : "competition";
        await api("PUT", `/api/membres/${membre.id}/type`, { type: nouveau });
        notifier(`${membre.nomComplet} : ${nouveau === "competition" ? "compétition" : "loisir"}.`);
      } else if (bouton.dataset.action === "statut") {
        const devientAdmin = !membre.admin;
        if (!confirm(devientAdmin ? `Donner les droits admin à ${membre.nomComplet} ?`
                                  : `Retirer les droits admin de ${membre.nomComplet} ?`)) return;
        await api("PUT", `/api/membres/${membre.id}/statut`, { admin: devientAdmin });
        notifier("Statut modifié.");
      } else if (bouton.dataset.action === "reinitialiser") {
        if (!confirm(`Effacer le mot de passe de ${membre.nomComplet} ? Il en choisira un nouveau à sa prochaine connexion.`)) return;
        await api("POST", `/api/membres/${membre.id}/reinitialiser`);
        notifier("Mot de passe réinitialisé.");
      } else if (bouton.dataset.action === "supprimer") {
        if (!confirm(`Supprimer définitivement le compte de ${membre.nomComplet} ?`)) return;
        await api("DELETE", `/api/membres/${membre.id}`);
        notifier("Compte supprimé.");
      }
      pageMembres();
    } catch (e) {
      notifier(e.message, true);
    }
  };
}

// ====================================================================== demarrage

(async function demarrer() {
  try {
    etat.membre = await api("GET", "/api/moi");
    demarrerSession();
  } catch {
    afficherConnexion();
  }
})();
