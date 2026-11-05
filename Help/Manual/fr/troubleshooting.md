# Dépannage et limites connues

## Aucune session
Vérifiez processus 64 bits, parcours D3D11/12 compatible, ReShade avec add-ons complets et chargement réel de CyGPUInspectorRS. Consultez ReShade.log et actualisez Connexions. Le standalone seul ne produit pas de capture. La démonstration sans jeu sépare un souci d’installation d’un souci de l’application.

## Temps absents ou événements perdus
Choisissez Pass Timing ou un niveau/ciblage adapté, puis attendez les résultats différés. Regardez pertes et capacité des timestamps dans État. Une fréquence nulle/inconnue ou une mesure manquante ne doit pas produire un faux temps. Le tampon d’événements est borné et perd des données plutôt que de bloquer le jeu. Un graphe issu d’événements partiels peut être incomplet.

## Aperçu noir ou indisponible
Vérifiez le direct, l’existence de la ressource, l’adaptateur commun et la compatibilité format/état. Essayez une plage/canal adapté et les réglages de profondeur/reverse-Z. Une frame hors ligne ordinaire n’a pas de pixels. L’export profond peut échouer pour un buffer sans invalider tous les autres fichiers.

## Compilation ou remplacement refusé
Vérifiez point d’entrée, étage/profil, signatures de ressources et compilateurs dans Outils. Conservez la paire DXC fournie pour SM6/signature DXIL. Lisez diagnostic et différences de compatibilité. Compiler ne prouve pas le même comportement. Restaurer annule un remplacement appliqué ; Annuler dans l’éditeur ne change que le texte source.

## IA / MCP indisponible
Vérifiez connexion CLI ou URL/modèle API et démarrage du serveur local. Les clés API durent une session. Pour MCP, démarrez le standalone, libérez la connexion de proxy unique et lisez niveau/refus côté hôte. Une analyse en attente demande une requête ultérieure, pas des appels concurrents répétés.

## Aide absente ou partiellement traduite
Restaurez le dossier Help généré complet à côté de l’EXE et rouvrez Help/index.html ; le dossier de travail ne doit pas compter. Le choix de langue est indépendant de l’application. Un article français absent utilise visiblement l’anglais. Une image absente donne un message de récupération sans empêcher la lecture.

## Limites fondamentales
Pas de replay, historique de pixel, exécution pas à pas de shader ni récupération du HLSL original. Les noms de passes sont déduits/donnés ; descripteurs et temps multi-listes ont des limites explicites. Historiques et tailles de captures sont bornés volontairement. Consultez [la référence des limites](help:ref-Limitations) avant de conclure à un dysfonctionnement.
