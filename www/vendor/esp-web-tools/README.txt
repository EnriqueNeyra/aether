ESP Web Tools 9.4.3 (self-hosted copy)
=====================================

Package:  esp-web-tools@9.4.3 (npm), https://github.com/esphome/esp-web-tools
Authors:  ESPHome maintainers
License:  Apache-2.0 (see LICENSE)

The *.js files are the package's prebuilt web bundle, copied unmodified from
https://unpkg.com/esp-web-tools@9.4.3/dist/web/ . Each file matched the
sha256 integrity hash unpkg lists for it at download time. The files import
each other by relative path, so keep their names and keep them together.

Entry point used by ../../index.html:
  <script type="module" src="vendor/esp-web-tools/install-button.js"></script>

LICENSE: the npm package does not ship a license file, and the upstream
repository had none at the 9.4.3 tag (package.json declares Apache-2.0).
The LICENSE here is the Apache License 2.0 text the project added later in
commit fff135bb4f3cd38c0a932d2b1564efb55c6c378f, its only revision.

The minified bundle also contains the package's dependencies, each under its
own license per its npm metadata: Lit (BSD-3-Clause), Material Web Components
@material/mwc-* (Apache-2.0) and @material/* (MIT), esptool-js (Apache-2.0),
improv-wifi-serial-sdk (Apache-2.0), pako (MIT AND Zlib), tslib (0BSD).
The upstream build does not keep their license comments in the .js files.

To upgrade: download every file listed at
https://unpkg.com/esp-web-tools@<version>/dist/web/?meta into this folder
(hashed chunk names change between versions, so remove the old ones), then
update this README.
