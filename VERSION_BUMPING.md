# Display Commander Version Bumping System

This project includes automated version bumping scripts to streamline the release process.

## Files

- `bump_version.ps1` - PowerShell script for version bumping
- `bump_version.bat` - Windows batch wrapper for easier use
- `src/addons/display_commander/version.hpp` - Version definitions file

## Usage

### Basic Usage

```bash
# Bump build number (0.5.1.1 → 0.5.1.2)
.\bump_version.ps1 build

# Bump patch version (0.5.1.1 → 0.5.2.0)
.\bump_version.ps1 patch

# Bump minor version (0.5.1.1 → 0.6.0.0)
.\bump_version.ps1 minor

# Bump major version (0.5.1.1 → 1.0.0.0)
.\bump_version.ps1 major
```

### Advanced Usage

```bash
# Bump and build
.\bump_version.ps1 patch --build

# Bump, build, and commit
.\bump_version.ps1 minor --build --commit

# Bump, build, commit, and tag
.\bump_version.ps1 patch --build --commit --tag

# Bump with custom commit message
.\bump_version.ps1 minor --build --commit --message "Add new feature X"
```

### Using Batch Wrapper

```bash
# Same commands but using .bat file
bump_version.bat patch --build --commit
bump_version.bat minor --build --commit --tag
```

## Version Numbering

The project uses semantic versioning with build numbers:

- **Major**: Breaking changes, major feature additions
- **Minor**: New features, backward compatible
- **Patch**: Bug fixes, small improvements
- **Build**: Incremental builds, same features

## What Gets Updated

When you run the version bump script, it automatically updates:

1. **Individual version numbers** in `version.hpp`:
   - `DISPLAY_COMMANDER_VERSION_MAJOR`
   - `DISPLAY_COMMANDER_VERSION_MINOR`
   - `DISPLAY_COMMANDER_VERSION_PATCH`
   - `DISPLAY_COMMANDER_VERSION_BUILD`

2. **Version string**:
   - `DISPLAY_COMMANDER_VERSION_STRING`

3. **Build timestamp** (automatically set by CMake):
   - `DISPLAY_COMMANDER_BUILD_DATE` - automatically set to current date during build
   - `DISPLAY_COMMANDER_BUILD_TIME` - automatically set to current time during build

## Options

- `--build`: Build the project after version bump
- `--commit`: Commit changes to git
- `--tag`: Create git tag for the release
- `--message`: Custom commit message

## Examples

### Development Build
```bash
.\bump_version.ps1 build --build
```
Increments build number and builds the project.

### Patch Release
```bash
.\bump_version.ps1 patch --build --commit --tag
```
Increments patch version, builds, commits, and creates a git tag.

### Minor Release
```bash
.\bump_version.ps1 minor --build --commit --tag --message "Add new display management features"
```
Increments minor version with custom commit message.

### Major Release
```bash
.\bump_version.ps1 major --build --commit --tag --message "Major rewrite with new architecture"
```
Increments major version for breaking changes.

## Git Integration

The script integrates with git for:
- **Commits**: Automatically stages `version.hpp` and commits with version message
- **Tags**: Creates annotated tags for releases (e.g., `v0.5.1.2`)
- **Messages**: Supports custom commit messages

## Safety Features

- **Confirmation**: Asks for confirmation before proceeding
- **Validation**: Validates version file format and git status
- **Error Handling**: Stops on errors and provides clear messages
- **Backup**: Git automatically tracks changes for easy rollback

## Troubleshooting

### PowerShell Execution Policy
If you get execution policy errors:
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Git Not Found
Make sure git is in your PATH or install Git for Windows.

### Build Failures
The script will stop if the build fails. Fix build issues before running again.

## Integration with CI/CD

The version bumping script can be integrated into CI/CD pipelines:

```yaml
# Example GitHub Actions step
- name: Bump version
  run: |
    powershell -ExecutionPolicy Bypass -File "bump_version.ps1" patch --build --commit --tag
```

This ensures consistent versioning across all environments.
