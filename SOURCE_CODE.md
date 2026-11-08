# Aster — Corresponding Source

Original Aster contributions: Copyright (C) 2026 ZiJie Chen. The software is distributed under GPL-3.0-only; see [LICENSE](LICENSE) and [COPYRIGHT.md](COPYRIGHT.md). Third-party code and datasets retain their applicable terms.

## Download source for a release

Open [Aster Releases](https://github.com/ChenCLian/Aster/releases) and select the version shown in Settings. Each binary release must provide:

- complete project source matching the program, including bundled third-party source, build/release scripts and notices;
- source archives or precise download instructions for dependencies required as Corresponding Source, with the versions, build recipes and patches used in each platform package.

Select the matching `v<version>` release tag and download **Source code (zip)** or **Source code (tar.gz)**. These GitHub archives contain the files committed at the release tag. The tag must match the published program and include all required project source and resources. If additional project source is needed, the release notes must link a complete source export. Use the source for the installed release; the current development branch may contain different code.

Source downloads must be accessible to binary recipients at no additional charge, with clear links beside the binary downloads. Dependency sources may be on a separate server if equivalent access is maintained. See GPL v3 section 6(d).

The runnable package includes `SOURCE_DEPENDENCIES.md` with exact release-asset links for that platform. Download the listed source archives and dependency records to obtain the versions, build recipes and patches used in the package. License and attribution files are included under `licenses/dependencies/`; dependency source archives are separate downloads.

## Build information

The project archive includes `docs/building.md` and [the third-party inventory](https://github.com/ChenCLian/Aster/blob/main/docs/third_party.md). [Build instructions are also available online](https://github.com/ChenCLian/Aster/blob/main/docs/building.md). Use the files in the matching source archive for a released version.

Personal configuration, user databases and build caches are excluded from the source archive. Configure local dependency paths from `config/local.pri.example`. Release build configurations, dependency recipes and required patches must remain available with the source materials.

## Redistribution

Preserve LICENSE, COPYRIGHT.md, this file and the original third-party notices. Binary redistribution requires the complete Corresponding Source under the applicable licenses, including required non-System-Library dependencies. A license text or a generic dependency homepage alone does not supply that source. Dataset redistribution is governed separately by the applicable source terms.

Maintainers can export a clean `Aster-source-export-<version>/` directory with `python tools/release/package_source.py --output ../release-source` from the project root. The exporter creates neither an archive nor a separate checksum file. Source file hashes remain in `PACKAGE_MANIFEST.json`. Windows packaging verifies and reuses matching dependency materials by default; run the collector separately when materials are missing or dependencies change. Linux/macOS packaging collects dependency sources and records before completing the archive/signature. They do not publish releases. See [dependency materials](https://github.com/ChenCLian/Aster/blob/main/docs/dependency_materials_zh.md) and [release maintenance](https://github.com/ChenCLian/Aster/blob/main/docs/release_maintenance_zh.md).
