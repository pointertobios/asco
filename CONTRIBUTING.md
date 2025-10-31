# Development & Contributing

Pull requests must be reviewed and approved by at least one project maintainer before merging.

## Feature Development

* Please create a issue to show your idea of new feature and ask for joining into this repo as a collaborator.

1. (Optional) Fork the repo if you are not a collaborator or prefer not to work directly in the main repository.
2. Create a new branch from the latest `main` branch. Name the new branch with the following format: `feature/<feature-name>`.
3. Develop your feature in the new branch.
4. Commit your changes and push them to the new branch.
5. Create a pull request to request to merge your changes into the `main` branch.

* New features must include comprehensive unit tests and documentation in either Chinese or English; otherwise, they will not be accepted.
  The documentation requirements are flexible — a high-level overview of the feature's functionality is sufficient.

## Feature branches lifecycle

* Only for this repo

1. Create a new branch from `main` for new feature.
2. Implement the feature.
3. Write unit tests for the feature.
4. Write documentation for the feature.
5. Merge the feature branch into `main` when the feature is ready.
6. Delete the feature branch.

* Everyone who had joined this repo can contribute on the feature branches.

## Versioning

The version number follows a three-segment format: `a.b.c`. The release should be tagged in Git using annotated tags (e.g., `1.2.0`).

1. **Breaking Change Release**
   * The `a` version is incremented.

2. **Scheduled Minor Release** (every 3 months):
   * The `b` version is incremented.
   * May include new features, improvements, and bug fixes accumulated since the last minor release.

3. **Patch Release** (as needed):
   * The `c` version is incremented.
   * Triggered by critical bug fixes or important feature additions outside of the scheduled minor release cycle.
