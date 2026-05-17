# Changelog

## [0.8.7](https://github.com/ziv/raytiles/compare/v0.8.6...v0.8.7) (2026-05-17)


### Bug Fixes

* skirt overlap configurations ([ec79e37](https://github.com/ziv/raytiles/commit/ec79e37830d72752ef64d2f857d58004ef813828))

## [0.8.6](https://github.com/ziv/raytiles/compare/v0.8.5...v0.8.6) (2026-05-16)


### Bug Fixes

* align c api with public c++ ones ([b472756](https://github.com/ziv/raytiles/commit/b472756532a67be0ca5cad0795c6612cc4f4cc03))
* rename configuration var ([b472756](https://github.com/ziv/raytiles/commit/b472756532a67be0ca5cad0795c6612cc4f4cc03))
* skirt and shaders communication ([b472756](https://github.com/ziv/raytiles/commit/b472756532a67be0ca5cad0795c6612cc4f4cc03))

## [0.8.5](https://github.com/ziv/raytiles/compare/v0.8.4...v0.8.5) (2026-05-16)


### Bug Fixes

* change url configuration format ([#63](https://github.com/ziv/raytiles/issues/63)) ([a262fe3](https://github.com/ziv/raytiles/commit/a262fe3bac0faf493b479adcedf513c31d501f47))

## [0.8.4](https://github.com/ziv/raytiles/compare/v0.8.3...v0.8.4) (2026-05-16)


### Bug Fixes

* clarify function using comment (cheap and honost) ([f6de3ab](https://github.com/ziv/raytiles/commit/f6de3ab1275675ac8115b03e08db80e3f9eb4661))
* clarify function using comment (cheap and honost) ([#61](https://github.com/ziv/raytiles/issues/61)) ([f6de3ab](https://github.com/ziv/raytiles/commit/f6de3ab1275675ac8115b03e08db80e3f9eb4661))
* pool shutdown can block for up to ~15 s per worker ([f6de3ab](https://github.com/ziv/raytiles/commit/f6de3ab1275675ac8115b03e08db80e3f9eb4661))
* tile_key hash — seed = 0 makes first XOR identity ([f6de3ab](https://github.com/ziv/raytiles/commit/f6de3ab1275675ac8115b03e08db80e3f9eb4661))
* TraceLog is fed runtime-generated strings via %s ([f6de3ab](https://github.com/ziv/raytiles/commit/f6de3ab1275675ac8115b03e08db80e3f9eb4661))

## [0.8.3](https://github.com/ziv/raytiles/compare/v0.8.2...v0.8.3) (2026-05-16)


### Bug Fixes

* altitude re-stream condition is inverted ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))
* c hrader in not c compatible ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))
* distance_to_horizon returns NaN for position.y &lt;= 0 ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))
* normalize_plane divides by zero ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))
* RaytilesConfigDefault() returns a struct that cannot construct a streamer ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))
* update check change in distance by xz ([11d3192](https://github.com/ziv/raytiles/commit/11d3192cd5abf99ec44ce65d5d589d335399a87d))

## [0.8.2](https://github.com/ziv/raytiles/compare/v0.8.1...v0.8.2) (2026-05-15)


### Bug Fixes

* minor tidy issues ([aa07528](https://github.com/ziv/raytiles/commit/aa0752814d9f3e93f5ddc3294be04059502bd03e))

## [0.8.1](https://github.com/ziv/raytiles/compare/v0.8.0...v0.8.1) (2026-05-15)


### Bug Fixes

* align c wrapper API with public c++ API ([#54](https://github.com/ziv/raytiles/issues/54)) ([4616144](https://github.com/ziv/raytiles/commit/46161446a7fbd1016e9b3b3df6e472e2b1f076fb))

## [0.8.0](https://github.com/ziv/raytiles/compare/v0.7.2...v0.8.0) (2026-05-15)


### Features

* API changed, configuration splited into specific conf structs with defaults ([a7f9a2c](https://github.com/ziv/raytiles/commit/a7f9a2cdcc60f25b5253c92a7edbeee4c37d12b6))
* change underlying api extracted renderer out of streamer, public API changed ([017558e](https://github.com/ziv/raytiles/commit/017558e916782a265c5b2d2ad7b1cd4917d7d357))


### Bug Fixes

* debug 3d use the same culling as draw ([75962d2](https://github.com/ziv/raytiles/commit/75962d2188b02b2885a0483ec8667f145cc1db22))
* debug use culling ([7738333](https://github.com/ziv/raytiles/commit/773833344ac066d5c0a0e87a38fc0ae5ff94752e))
* fix the function signature and add missing struct item ([233e5d2](https://github.com/ziv/raytiles/commit/233e5d2ae5e5fd1e56e79fb9f1c7fdc47b7537d3))
* make the debug methods use the same struct ([afa7077](https://github.com/ziv/raytiles/commit/afa70777618ac5ae9ea29eb9c3bb5bfba00bc8c2))
* remove from rendered tiles those that not in frustum, partially solve [#49](https://github.com/ziv/raytiles/issues/49) ([f734500](https://github.com/ziv/raytiles/commit/f734500f98c87a772263a303728f996fc55e415e))
* simplify pool config to use URL ([a36045b](https://github.com/ziv/raytiles/commit/a36045b3c296a450f1841b06cf25815b398a5d54))
* split configuration ([#52](https://github.com/ziv/raytiles/issues/52)) ([a7f9a2c](https://github.com/ziv/raytiles/commit/a7f9a2cdcc60f25b5253c92a7edbeee4c37d12b6))
* the order of filtering items in debug view ([ea0dffb](https://github.com/ziv/raytiles/commit/ea0dffbd170af5a5ed7859b66b347c8fbf823e0d))

## [0.7.2](https://github.com/ziv/raytiles/compare/v0.7.1...v0.7.2) (2026-05-14)


### Bug Fixes

* add cancelation mechanism to downloader, partially solve [#48](https://github.com/ziv/raytiles/issues/48) ([404f1ff](https://github.com/ziv/raytiles/commit/404f1ff02a72cd087be8fa9d9e264946f9234239))
* add notify_all after request stop to stop pool ([c49ce94](https://github.com/ziv/raytiles/commit/c49ce9452659811145d12c7185075d349fda794e))
* add validation to GetShaderLocation results ([85c62cc](https://github.com/ziv/raytiles/commit/85c62cc6c4ac7d9491eebd326f29c18e67837f1c))
* add warnings to the build system, set raylib as sytem to silence warnings ([3114e22](https://github.com/ziv/raytiles/commit/3114e22b4c7e334cd4847f63eb6e4a966d12717b))
* fog_end doubles as the culling radius replaced with real horizon calculation ([0920ead](https://github.com/ziv/raytiles/commit/0920ead2e0ca61b2c20a571a93eb3421752e3080))
* remove the compiler enforced value in EMSCRIPTEN ([fb47380](https://github.com/ziv/raytiles/commit/fb4738022d38dc4f32cbaa468bea22c74d0e1d5d))
* reorder files and add installation in cmake files ([48afa63](https://github.com/ziv/raytiles/commit/48afa6318be288d9aed484250e8d30ead2f60329))
* replace replace_all with replace in downloader ([2c90f25](https://github.com/ziv/raytiles/commit/2c90f2570429950148faf2f6d051f58756763147))

## [0.7.1](https://github.com/ziv/raytiles/compare/v0.7.0...v0.7.1) (2026-05-14)


### Bug Fixes

* add another distance function for xz, issue [#34](https://github.com/ziv/raytiles/issues/34) ([d893bbd](https://github.com/ziv/raytiles/commit/d893bbd871c77e8479064d23996a939d8d6f1fbb))

## [0.7.0](https://github.com/ziv/raytiles/compare/v0.6.2...v0.7.0) (2026-05-14)


### Features

* extend support for zoom levels 9 to 15 ([#40](https://github.com/ziv/raytiles/issues/40)) ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))
* now support zoom level 10 ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))


### Bug Fixes

* add input validation for zoom levels ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))
* limit the build to sandbox target ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))
* remove pImpl for gain more pref ([#43](https://github.com/ziv/raytiles/issues/43)) ([1c67aef](https://github.com/ziv/raytiles/commit/1c67aef4ce25db61a33d2ccc7fa8ff46458f0052))
* remove the pimpl manager class and move all impl directly into streamer ([1c67aef](https://github.com/ziv/raytiles/commit/1c67aef4ce25db61a33d2ccc7fa8ff46458f0052))
* update config to work with zoom 9 as default ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))
* update sandbox to work with zoom 9 ([bc497b4](https://github.com/ziv/raytiles/commit/bc497b407f18a4ab27c695a537d26461fcf77977))

## [0.6.2](https://github.com/ziv/raytiles/compare/v0.6.1...v0.6.2) (2026-05-14)


### Bug Fixes

* code review items ([#39](https://github.com/ziv/raytiles/issues/39)) ([a9749b5](https://github.com/ziv/raytiles/commit/a9749b57a209e2fd2bbca7405812b2fc99b12ca1))
* pimpl signature - remove const ([a9749b5](https://github.com/ziv/raytiles/commit/a9749b57a209e2fd2bbca7405812b2fc99b12ca1))
* remove redecode image ([a41174c](https://github.com/ziv/raytiles/commit/a41174c66aca74df6e9dddb8901e51e72be477d9))
* sandbox example ([a9749b5](https://github.com/ziv/raytiles/commit/a9749b57a209e2fd2bbca7405812b2fc99b12ca1))
* typo in fog_color_loc ([a9749b5](https://github.com/ziv/raytiles/commit/a9749b57a209e2fd2bbca7405812b2fc99b12ca1))

## [0.6.1](https://github.com/ziv/raytiles/compare/v0.6.0...v0.6.1) (2026-05-13)


### Bug Fixes

* add debug data ([f3e749c](https://github.com/ziv/raytiles/commit/f3e749c1fae4f67a93ea1b607c8ae1f01eccf6b3))
* clamp height in horizon caclulation to avoide collapse to zero ([3b782b9](https://github.com/ziv/raytiles/commit/3b782b9704e4ee1f549516c907dab150ee0c02b4))
* frustum culling aware rendering ([5841648](https://github.com/ziv/raytiles/commit/58416482853d33b33e3e3371d6e77bdf8a4ddf62))
* **main:** udpate debug data 2d ([4b8f8b1](https://github.com/ziv/raytiles/commit/4b8f8b1fef23b18b20bd8ee6944859fb2f1ca15c))
* release pelase config file ([d06e678](https://github.com/ziv/raytiles/commit/d06e678a919d8d0aafe61fef6b06799aae9eabc5))
* remove hardcoded zoom in ctor and fix c wrapper ([88709cc](https://github.com/ziv/raytiles/commit/88709cc1cd4441c219d2ff81ee0a46fce8f9de9f))
* sandbox app ([d3c5f9a](https://github.com/ziv/raytiles/commit/d3c5f9ab7ddd1af0002f3918a0c1627e200d0915))
* set aabb max height ([81e20e7](https://github.com/ziv/raytiles/commit/81e20e791dd528a6086a8ff11b1e4cc86b6fc2b9))
* use frustum culling to decide which tile to render ([#32](https://github.com/ziv/raytiles/issues/32)) ([9733f2e](https://github.com/ziv/raytiles/commit/9733f2ed202511e3563bc9d93318089d6d32b419))

## [0.6.0](https://github.com/ziv/raytiles/compare/v0.5.2...v0.6.0) (2026-05-13)


### chore

* release 0.6 ([a708974](https://github.com/ziv/raytiles/commit/a7089744c130a6beb7b98c2c643da3ac0d14278f))

## [0.5.2](https://github.com/ziv/raytiles/compare/v0.5.1...v0.5.2) (2026-05-09)


### Bug Fixes

* path to manifest version ([ce46a8c](https://github.com/ziv/raytiles/commit/ce46a8c9d26017106f956be3a5c30b94ec15d5a0))
