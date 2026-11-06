---
id: ref-translating
title: Traduire l’interface
description: Détails techniques : Retrouver les panneaux, suivre les sélections et régler apparence ou langue.
section: Référence technique
order: 36
parent: navigation
sourceRevision: 6c5e7d61f6e6
---

# Traduire CyGPUInspector {#translating-cygpuinspector}



L'anglais est la langue de référence. Le code, les commentaires, cette documentation et chaque
chaîne de l'interface sont écrits en anglais d'abord, et c'est l'anglais qui fait foi quand une
traduction le contredit.

---

## 1. L'interface {#1-the-interface}

### L'idée {#the-idea}

Une clé, c'est **la phrase anglaise elle-même**, pas un identifiant inventé du genre
`ui.shaders.title`. Cette seule décision est ce qui fait marcher le reste :

* une traduction absente ou à moitié faite affiche de l'anglais — jamais une étiquette vide, jamais
  une clé brute ;
* il n'y a rien à nommer : envelopper une nouvelle chaîne coûte un appel et aucune comptabilité ;
* un traducteur lit l'anglais et écrit l'autre langue à côté, avec le contexte sous les yeux plutôt
  que dans un document séparé.

```cpp
ImGui::TextUnformatted(Tr("Applications running CyGPUInspectorRS"));
if (ImGui::Button(TrId("Capture now")))
    StartDeepCapture();
```

`Tr` sert au texte. `TrId` sert à tout ce qu'ImGui utilise aussi comme identifiant — titre de
fenêtre, bouton, colonne de table. Il ajoute l'anglais en suffixe caché `###`, et ImGui ne hache
que la partie après `###` : **une disposition sauvegardée survit donc à un changement de langue**.
Un utilisateur anglais et un utilisateur français partagent un seul `CyGPUInspectorApp.ini`.

### Ajouter une langue {#adding-a-language}

1. Copier `Lang/template.json` vers `Lang/<code>.json`, où `<code>` est par exemple `de` ou `ja`.
2. Mettre ce code dans `language`, et dans `name` le nom que la langue se donne à elle-même —
   `Deutsch`, pas `Allemand`. C'est ce que le menu affiche.
3. Remplir les valeurs. En laisser une vide garde l'anglais pour cette chaîne.
4. Relancer l'application. Aucune recompilation : le dossier est scanné au démarrage et la langue
   apparaît sous **Affichage → Langue**.

Modifier un catalogue du dépôt (`Lang/`) demande une compilation, parce que c'est elle qui le copie
à côté de l'exécutable — un simple `build.cmd` suffit, et il copie même si rien d'autre n'a changé.
Modifier directement la copie à côté de l'exécutable ne demande qu'un redémarrage.

Le choix est retenu dans `CyGPUInspectorApp.settings.json`, à côté de l'exécutable.

### Règles qu'une traduction doit respecter {#rules-a-translation-has-to-respect}

* **Garder chaque `%s`, `%u`, `%llu`, `%.2f`, dans le même ordre.** C'est là que les nombres et les
  noms sont insérés. En perdre un, ou en intervertir deux, fait afficher n'importe quoi à
  l'interface, voire lire au-delà des arguments qu'on lui a donnés.
* **Ne rien traduire après `###`.** C'est l'identité du widget, pas du texte.
* `\n` est un retour à la ligne. Le garder là où l'anglais le met.
* Le texte a le droit d'être plus long. Les panneaux se réajustent ; rien n'est calé sur une
  largeur fixe.

### Tenir un catalogue à jour {#keeping-a-catalogue-up-to-date}

L'application écrit un modèle à partir d'une **exécution réelle** : **Affichage → Langue → Écrire un
modèle de traduction…** vide dans `Lang/template.json` chaque chaîne réellement demandée depuis le
démarrage, avec les traductions de la langue courante déjà remplies. Ouvrez d'abord les panneaux
qui vous intéressent : une chaîne que l'interface n'a jamais affichée est une chaîne que
l'exécution n'a jamais vue.

Le même menu indique combien de chaînes ont été affichées et combien n'avaient pas de traduction,
ce qui permet de distinguer « fini » de « qui a l'air fini ».

### Les overlays des add-ons {#the-add-on-overlays}

Les deux overlays ReShade — celui de l'agent de capture et celui de CyGPUInjector — passent par le
même `Tr` et les mêmes catalogues. Ils sont petits : quelques dizaines de chaînes chacun.

---

## 2. La documentation {#2-the-documentation}

La langue de l’application et celle de la documentation sont indépendantes. Utilisez Affichage > Langue pour les widgets et le sélecteur de l’en-tête du lecteur pour le manuel. L’anglais est la langue originale et par défaut de la documentation. Les éditions anglaise et française sont complètes ; choisir le français conserve la page et la section.

## 3. Ce qui n'est délibérément pas traduit {#3-what-is-deliberately-not-translated}

* **Le code, et chaque commentaire dedans.** Une seule langue pour les sources, et c'est l'anglais.
* **Les lignes de journal et les messages d'erreur des add-ons.** Ils finissent dans `ReShade.log`,
  qui est ce qu'on colle dans un rapport de bug ; un journal en anglais est lisible par qui aide.
* **Les noms d'outils MCP et leurs descriptions.** C'est une surface d'API sur laquelle les agents
  s'appuient.
* **Les chaînes purement de format** comme `%s`, `RT%u` ou `%.3f ms`. Il n'y a rien à y traduire, et
  le catalogue les omet plutôt que de les répéter.

---

## 4. Où ça vit dans le code {#4-where-this-lives-in-the-code}

Les catalogues sont des fichiers JSON modifiables dans `Lang/` à côté de l’exécutable. Conservez les clés anglaises et utilisez la création de modèle pour découvrir les nouveaux libellés.
