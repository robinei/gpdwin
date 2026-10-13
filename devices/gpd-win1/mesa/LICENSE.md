# Licence of this folder

- The Mesa patches (`00*.patch`, `vk13/`) change Mesa's crocus and hasvk drivers, which are MIT-licensed; the
  patches are under the same **MIT** licence (`LICENSE` at the repo root). Copyright of Mesa stays with its authors.
- `PKGBUILD.arch`, `lib32/PKGBUILD.arch` and the `keys/` are Arch Linux's packaging (**0BSD**,
  `LICENSES/0BSD.txt`); our `PKGBUILD`s are derived from them and are MIT/0BSD.
- Reports, benchmarks and test programs (`reports/`, `bench.sh`, ...) are ours, MIT.
- The Rust crate archives the build downloads (`*.tar.gz`) are not in the repo; they carry their own licences.
