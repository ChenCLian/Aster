#ifndef ASTER_RELEASE_VERSION_H
#define ASTER_RELEASE_VERSION_H
#include <QStringList>
#include <optional>
namespace aster::updates {
// SemVer precedence, including numeric prerelease identifiers. Build metadata
// does not affect ordering; no conversion to bounded machine integers.
struct ReleaseVersion {
    QString major, minor, patch;
    QStringList prerelease;
    static std::optional<ReleaseVersion> parse(QString text);
    int compare(const ReleaseVersion &other) const;
};
}
#endif
