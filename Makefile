.PHONY: sim-passport sim-iphone sim-mac preview-ui plugin-test plugin-package web web-dev web-build web-tunnel web-test

web:
	python3 tools/web.py serve

web-dev:
	python3 tools/web.py dev

web-build:
	python3 tools/web.py build

web-tunnel:
	python3 tools/web.py tunnel

web-test:
	python3 tools/web.py prepare
	npm test --prefix web
	npm run test:ui --prefix web

sim-passport:
	tools/simulate.sh

sim-iphone:
	tools/iphone.sh

sim-mac:
	tools/mac.sh

preview-ui:
	cmake -S sim/ui_preview -B build/ui-preview
	cmake --build build/ui-preview -j 8
	mkdir -p sim/shots/ui-redesign
	build/ui-preview/preview sim/shots/ui-redesign

plugin-test:
	node tests/test_passport_plugin.mjs
	node tests/test_passport_relay.mjs
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_relay_install.py
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_plugin_migration.py
	PYTHONDONTWRITEBYTECODE=1 python3 tests/test_passport_plugin_package.py

plugin-package: plugin-test
	mkdir -p build/openclaw
	cd tools/openclaw && npm pack --ignore-scripts --pack-destination ../../build/openclaw
