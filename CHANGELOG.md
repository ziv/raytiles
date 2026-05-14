# Changelog

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
