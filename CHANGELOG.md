# Changelog

All notable changes to the `apollo-psp` project will be documented in this file. This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]()

## [v1.3.0](https://github.com/bucanero/apollo-psp/releases/tag/v1.3.0) - 2024-02-17

### Added

* Proper save resigning using KIRK engine CMD5
  - Uses unique per-console Fuse ID
  - Fixes save ownership in games like Gran Turismo
* Manage PS1 Virtual Memory Card images (VMC)
  - Supports `.VMP` and external formats (`.MCR`, `.VM1`, `.BIN`, `.VMC`, `.GME`, `.VGS`, `.SRM`, `.MCD`)
  - List, import, and export PS1 saves inside VMC images
  - Import - Supported formats: `.MCS`, `.PSV`, `.PSX`, `.PS1`, `.MCB`, `.PDA`
  - Export - Supported formats: `.MCS`, `.PSV`, `.PSX`
* New PSP cheat codes
  - Monster Hunter Freedom Unite (ULUS10391/ULES01213)
  - Monster Hunter Portable 2nd G (ULJM05500)
* Custom save decryption
  - Patapon 3 (UCUS98751/UCES01421)
  - Monster Hunter Freedom Unite (ULUS10391/ULES01213)
  - Monster Hunter Portable 2nd G (ULJM05500)
  - Monster Hunter Portable 3rd (ULJM05800)
* Custom checksum support
  - Monster Hunter Freedom Unite (ULUS10391/ULES01213)
  - Monster Hunter Portable 2nd G (ULJM05500)
  - Monster Hunter Portable 3rd (ULJM05800)

### Misc

* Updated Apollo Patch Engine to v0.7.0
  - Add `jhash`, `jenkins_oaat`, `lookup3_little2` hash functions
  - Add `camellia_ecb` encryption
  - Add Monster Hunter 2G/3rd PSP decryption
  - Add RGG Studio decryption (PS4)
  - Add Dead Rising checksum

## [v1.2.0](https://github.com/bucanero/apollo-psp/releases/tag/v1.2.0) - 2023-10-30

### Added

* Support `ef0` storage (PSP Go internal memory)
* Added AHX background music player
* Added on-screen keyboard
* Added URL Downloader (`Network Tools`)
* Auto-detect `X`/`O` button settings
* Compress `.ISO` files to `.CSO`
* Decompress `.CSO` files to `.ISO`
* Added Save-game Key database for PSP games
* New PSP cheat codes
  - Persona (ULUS10432)
* New PSP unlock patches
  - InviZimals: Shadow Zone (UCES01411, UCES01581, UCUS98760)
  - InviZimals: The Lost Tribes (UCES01525)
  - SOCOM: Fire Team Bravo 2 (UCUS98645)
* Custom checksum support
  - InviZimals: Shadow Zone (UCES01411, UCES01581, UCUS98760)
  - InviZimals: The Lost Tribes (UCES01525)

### Fixed

* Online DB: fixed file cache
* Simple Webserver: fixed folder links
* Fixed timestamp on debug logs

### Misc

* Updated `apollo-lib` (Patch Engine) to v0.5.5
  - Add host callbacks (username, lan mac, wlan mac, sys name, psid, account id)
  - Add `murmu3_32` hash function
  - Add MGS5 decryption PS3/PS4
  - Add Castlevania:LoS checksum
  - Add Rockstar checksum
  - Fix SaveWizard Code Type C
  - Fix `right()` on little-endian platforms

## [v1.0.0](https://github.com/bucanero/apollo-psp/releases/tag/v1.0.0) - 2023-04-30

### Added

* Hex Editor for save-data files
* Improved save-game Web Server (`Bulk Save Management`)
  - Backup and download saves as .Zip
* Network Tools
  - Simple local Web Server (full access to console memory stick)
* New PSP cheat codes
  - BlazBlue: Continuum Shift II
* Custom checksum support
  - BlazBlue: Continuum Shift II

## [v0.7.0](https://github.com/bucanero/apollo-psp/releases/tag/v0.7.0) - 2023-04-02

First public release.
