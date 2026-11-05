# Working rules for this repository

These rules are for anyone, human or Claude, making changes here.

- **No merge without a real CI pass.** A pull request merges only after a real CI run (`.github/workflows/`) passes on
  its head commit. A run that GitHub refused (account billing lock, "startup failure", jobs with no runner or no steps)
  is not a test result. While CI can't run, run the same commands locally and paste the results into the PR
  description, listing any step you couldn't run. That is a record, not a replacement for CI: the PR still waits.
- **Never push to the archived `saijinjun/DigimonWorld3-Recomp`.** It is the old private repository, kept as a
  read-only backup. All work happens here.
- **No game data and no signing keys in the repository.** Nothing from the disc goes in: images, maps, audio, text, RAM
  dumps, save states or a prepared game module. The same holds for any APK or pack that carries them. The Android
  signing key comes from `-PkeystoreFile` locally or from the repository secrets in CI. A keystore or its password is
  never committed.
- **One pull request per batch of small fixes.** Group related small fixes into one PR instead of opening one PR each.
