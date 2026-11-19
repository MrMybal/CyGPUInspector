---
id: automation
title: Piloter l’application avec une IA ou des scripts
description: Naviguer et agir dans l’application réelle avec les commandes partagées.
section: Guide utilisateur
order: 14
parent: mcp
sourceRevision: f7286812af82
---

# Piloter l’application avec une IA ou des scripts {#application-automation}

Le catalogue partagé contient 111 opérations : 34 commandes GPU et 77 commandes applicatives.
L’assistant intégré, le CLI et MCP appellent les mêmes fonctions réelles sur le thread de
l’interface. Les actions respectent la connexion, la capture, les droits et le mode d’approbation.

## Découvrir et naviguer {#discover}

Commencez par `get_application_state` : sélection, connexion/capture, thèmes/langues installés
et dossier d’export. `list_sessions` fournit les identités processus/périphérique.
`connect_session` en choisit une explicitement ; `disconnect_session` conserve le travail de
l’éditeur. `open_capture` prend un nom de `list_captures` ; `close_capture` retrouve la connexion
en direct. Le suivi, la synchronisation, les captures, comparaisons et snapshots restent disponibles.

`select_shader`, `select_resource`, `select_event` et `select_pass` changent la vraie sélection.
`set_filters`, `set_workspace` et `focus_panel` pilotent la navigation. `get_panel_layout` liste
les noms stables des panneaux ; `dock_panel` organise onglets et divisions. Réinitialisation et
restauration de disposition gardent des sauvegardes. `set_appearance` et `set_language`
enregistrent les mêmes préférences que les menus.

## Inspecter et modifier {#edit}

L’aperçu expose canaux, profondeur, plages, mip/slice, zoom et ajustement. Ses commandes
démarrent, arrêtent, figent et exportent l’image réelle ; l’export PNG peut échouer tant qu’aucune
image n’est prête. L’historique temporel contient des résumés conservés, pas des captures
complètes rejouables. Sélectionnez une frame/passe disponible sans inventer les données absentes.

Les notes proposent noms, tags et annotations identifiées comme produites par IA. Modifier ou
supprimer une annotation exige son texte actuel attendu. Une sauvegarde `.notes` ne se restaure
que pour sa signature de shader d’origine. Le renommage conserve l’identité de la passe déduite.
Les commandes de mods listent les changements, sélectionnent les entrées, exportent un nouveau
paquet et vident la liste avec un paquet de récupération. Vider cette liste n’annule pas les
changements dans le jeu. Les profils peuvent enregistrer et réappliquer leur état existant.

Lisez `get_editor_state` avant `set_editor_text` : transmettez son `sha256` comme
`expected_sha256`. Un changement concurrent est refusé avant modification. Le remplacement
sauvegarde l’ancien texte et participe à l’annulation/rétablissement natif. Compilez le vrai
document et examinez les diagnostics avant l’injection contrôlée. Import/export HLSL et réglages
d’affichage sont disponibles ; compiler ne prouve pas un rendu identique. Une capture hors ligne
ne peut pas recevoir un remplacement de shader en direct.

## Assistant et permissions {#assistant-controls}

Préférences sans secret, connexion, envoi/annulation de messages, tâches shader, export et
réinitialisation de conversation ainsi que statut sont exposés. Modifier le fournisseur exige
la déconnexion ; un état occupé refuse les actions incompatibles. Une tâche shader ne démarre
pas récursivement pendant un tour de chat actif. Nouvelle conversation garde une sauvegarde
du transcript et refuse tant que le tour précédent termine son arrêt.

Read Only permet l’inspection normale. Debug Control ajoute les commandes d’espace de travail.
Shader Modification est exigé pour HLSL, le contenu des conversations et la configuration du
fournisseur. L’assistant conserve Automatic / Ask / Full, les approbations et budgets réels.
Un agent ne peut ni s’accorder des droits, ni lire les champs de clés API, ni connecter un compte.
Aucune commande n’évalue de script, ne lance de terminal ni ne lit des fichiers arbitraires.

## Récupérer le travail et utiliser le CLI {#recovery}

Sauvegardes et exports créent de nouveaux fichiers privés dans `Automation/`, près des captures.
Ce sont des données utilisateur exclues des paquets. `list_automation_files` les liste. Un import
accepte un nom de fichier avec le suffixe `.hlsl`, `.layout` ou `.notes` attendu, jamais un chemin
extérieur. Vous pouvez y placer explicitement un document HLSL à importer.

La fermeture refuse les changements enregistrés tant qu’ils ne sont pas exportés ou effacés
avec sauvegarde, puis conserve le brouillon et le transcript. Les erreurs font foi ; ne rejouez
jamais une modification pour récupérer une référence de résultat expirée.

```powershell
.\CyGPUInspectorMCP.exe invoke get_application_state '{}'
.\CyGPUInspectorMCP.exe describe set_editor_text
.\CyGPUInspectorMCP.exe invoke-file set_filters filters.json
```

`filters.json` peut contenir `{"shaders":"stage:ps","used_this_frame":true}`. Découverte et schémas
fonctionnent sans interface ; l’exécution s’adresse à l’application ouverte. Suivez toutes les
pages MCP `nextCursor`, ou recherchez/décrivez/appelez en mode compact. Consultez
[la connexion et les permissions](doc:mcp).
