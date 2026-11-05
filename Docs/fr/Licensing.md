# Licences et distribution

CyGPUInspector et son plugin Unreal restent sous **GNU AGPL v3 ou ultérieure**.
Les textes applicables sont [LICENSE](../../LICENSE) et
[LICENCES/CyGPUInspector-AGPL-3.0.txt](../../LICENCES/CyGPUInspector-AGPL-3.0.txt).
Les copyrights et notices tiers sont conservés intégralement, dans leur langue originale.
[THIRD-PARTY.md](../../THIRD-PARTY.md) inventorie les composants et leurs conditions.

## Périmètre des SDK

L’assistant, les transports fournisseurs et le serveur MCP sont des implémentations
C++ natives de CyGPUInspector. **CyToolsCore** et **CyUEMCPSDK** ne sont ni embarqués,
ni installés, ni requis. Aucun compagnon Python ou runtime d’assistant n’est distribué.

## Contenu à distribuer

Chaque paquet possède un dossier canonique **LICENCES/** avec les textes complets
du produit et de ses dépendances, ainsi que **SOURCES.txt** pour les provenances.
Le dossier historique **Licenses/** reste présent dans les paquets binaires pour
compatibilité ; ce n’est pas l’index canonique.

Chaque plugin Unreal copiable séparément possède son propre **LICENCES/** :
licence du produit, ReShade, SDK ReShade et en-têtes ImGui de l’add-on.
Les notices complètes de ReShade restent aussi à côté de sa DLL.
`FilterPlugin.ini` les inclut dans la sortie `BuildPlugin` ; les modules existants
les déploient en fichiers libres **NonUFS** dans les jeux empaquetés.

Lorsque DXC est inclus, ses fichiers originaux `LICENCE-MIT.txt`, `LICENSE-LLVM.txt`,
`LICENSE-MS.txt` et `ORIGIN.txt` accompagnent les binaires dans
**LICENCES/DirectXShaderCompiler/**. Gardez la version et les DLL sélectionnées ;
une évolution de l’interface ne sélectionne pas implicitement un autre compilateur.

`Tools/package_release.py` compare les notices octet par octet à leurs sources,
vérifie le staging, relit le ZIP et contrôle son CRC. Une notice absente ou modifiée
fait échouer le packaging. Pour les notices en commentaire d’en-tête source,
le commentaire complet est extrait sans ses marqueurs ; gardez source et texte
généré synchronisés.

## Mise à jour et retour arrière

Compilez l’application normalement, puis reconstruisez chaque plugin réellement
livré avec `RunUAT BuildPlugin` pour le moteur correspondant. Empaquetez dans un
dossier distinct pour conserver la version précédente :

```powershell
python Tools/package_release.py --plugin-builds C:/CyGPUInspectorBuild --dxc <dossier-DXC> --output-root build/Release
```

Vérifiez le ZIP réel, pas seulement les sources. Exécutez les tests depuis
`TestWithoutAGame/`, puis vérifiez initialisation MCP et découverte des outils
avec l’exécutable livré. La validation d’un moteur nécessite sa compilation réelle.
Captures dans un vrai jeu et appels à un fournisseur payant demandent leurs propres
essais. Le packager refuse d’écraser un staging ou ZIP existant ; choisissez un autre
dossier à chaque reconstruction.

Extrayez une mise à jour dans un nouveau dossier. Conservez `CyGPUInspectorApp.ini`,
`CyGPUInspectorApp.settings.json`, bases, profils et captures utilisateur ; ils ne
font pas partie de l’archive de distribution. Préservez les paramètres Unreal et
chemins externes configurés. Pour revenir en arrière, restaurez les anciens fichiers
application/plugin en conservant les données utilisateur. Les préférences d’apparence
s’ajoutent aux réglages existants, sans nouvelle configuration de l’assistant.

L’aide intégrée est distribuée sous `App/Help` avec ses textes, traductions et figures.
Conservez tout ce dossier lors d’une copie de l’application.
