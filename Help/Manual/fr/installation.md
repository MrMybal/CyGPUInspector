# Installation et première connexion

## Paquet portable
Extrayez toute l’archive Windows x64 dans un dossier accessible en écriture. Gardez ensemble App, Addons, TestWithoutAGame, Unreal et les notices. Lancez App/CyGPUInspectorApp.exe. Conservez App/Help à côté de l’exécutable : ce dossier contient le guide, ses deux langues et toutes les illustrations. La lecture utilise un navigateur moderne, sans serveur Web, runtime embarqué, Node.js ni compte en ligne.

## Essayer sans jeu
Lancez TestWithoutAGame/Demo.cmd. Il démarre un producteur synthétique et connecte l’application. Explorez la timeline, les shaders et l’aperçu d’une ressource. Fermez ensuite les deux programmes. TestWithoutAGame/RunTests.cmd exécute les suites de tests natives incluses ; c’est un diagnostic, pas un test de compatibilité avec un vrai jeu.

## Connecter un vrai jeu
1. Installez la version de ReShade avec prise en charge complète des add-ons, selon la procédure normale compatible avec le jeu.
2. Placez Addons/CyGPUInspectorRS.addon64 à côté de l’installation ReShade du jeu, dans son emplacement de recherche des add-ons.
3. Démarrez le jeu et vérifiez dans ReShade que l’add-on est chargé. C’est ReShade qui le charge.
4. Ouvrez le standalone, actualisez Connexions avec F5 et choisissez Connecter sur le processus/périphérique voulu.
5. Choisissez un [niveau de suivi](help:capture), puis attendez les frames complètes et les mesures différées.

Plusieurs sessions peuvent apparaître. Vérifiez l’exécutable, le PID, l’API et l’adaptateur plutôt que la position dans la liste. Une session arrêtée peut conserver sa dernière frame à l’écran ; regardez la barre d’état avant d’attendre une action en direct.

## Mettre à jour
Extrayez la nouvelle version dans un autre dossier. Sauvegardez vos réglages, le layout, Database, Games, Captures, Images et vos mods avant de déplacer les données à conserver. Gardez les DLL et le dossier Help complets plutôt que de copier seulement l’EXE. Les clés API ne sont pas enregistrées dans les réglages. Conservez les licences lors d’une redistribution.

Pour Unreal, utilisez [les instructions du plugin](help:unreal). Un jeu qui refuse ReShade reste incompatible avec ce parcours de capture. Vérifiez le chargement normal de l’add-on ; l’inspecteur ne contourne pas les protections.
