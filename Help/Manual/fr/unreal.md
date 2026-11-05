# Capturer depuis Unreal Engine

## Installer le bon plugin
Choisissez Unreal/UE5.3 ou Unreal/UE5.8 selon le moteur utilisé. Copiez CyGPUInspector dans Plugins du projet. Un projet Blueprint seul nécessite le plugin précompilé pour ce moteur ; un projet C++ peut compiler une source compatible. L’intégration source vise Unreal 5.3+, mais un binaire d’une autre version n’est pas interchangeable.

Conservez les binaires ReShade/add-on de ThirdParty et les licences du plugin. Dans Paramètres du projet > Plugins > CyGPUInspector, vérifiez leurs chemins et celui du standalone. Redémarrez l’éditeur : le loader intervient avant la création du périphérique graphique. Consultez LogCyGPUInspectorLoader si le chargement échoue.

## Capturer
Utilisez le bouton de barre d’outils, la commande console ou l’interface Blueprint décrite dans [la référence Unreal](help:ref-UnrealPlugin). Gardez le viewport cible visible et actif. L’intégration marque son rendu pour limiter les commandes profondes et choisir l’image finale ; la timeline runtime du standalone peut décrire une frame plus large.

Un viewport inactif peut ne redessiner qu’après un changement. Le plugin demande un rafraîchissement et peut expirer en attendant une frame adéquate. Contrôlez la portée de la capture et les noms de ressources plutôt que de supposer que toutes les fenêtres sont incluses.

## Configurations de build
Le loader vise Editor, Development et DebugGame. Shipping désactive le chargement/capture actif ; les nœuds Blueprint peuvent rester sans activer l’inspection. Sur les autres plateformes ils sont inactifs. Les dépendances runtime sont déclarées pour les configurations compatibles.

Load In Game dans les paramètres contrôle le chargement normal en jeu. -NoCyGPUInspector le désactive pour un lancement ; -CyGPUInspector force le parcours prévu. Logs/réglages sont placés sous Saved/CyGPUInspector du projet, pas dans l’installation commune du moteur.

Ne changez pas le descripteur pour rebaptiser un ancien binaire. Recompilez pour le moteur réel ou utilisez le paquet fourni correspondant.
