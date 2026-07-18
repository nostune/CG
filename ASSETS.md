# Asset Setup And Distribution

Large development assets are not stored in Git. The current startup scene lists
its required files in `config/assets.required.txt`.

Check the local asset set:

```powershell
.\scripts\check-assets.ps1
```

The historical `OuterWIlds_release.zip` package is currently the most complete
local snapshot. Restore only the files required by the current scene with:

```powershell
.\scripts\restore-assets.ps1
```

Pass `-ArchivePath` when the archive is stored elsewhere. Existing files are
left untouched unless `-Force` is supplied.

## Distribution Policy

- The Outer Wilds original soundtrack is intentionally excluded from the
  required manifest and restore process. It must not be shipped without an
  explicit distribution license.
- Third-party models and textures recovered from historical packages are for
  local development only until their source and license are documented.
- Before publishing a demo or Steam build, replace or license every asset and
  add its attribution and distribution terms to an asset registry.
