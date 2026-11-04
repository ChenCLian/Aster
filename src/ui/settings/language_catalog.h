#ifndef ASTER_LANGUAGE_CATALOG_H
#define ASTER_LANGUAGE_CATALOG_H
#include <QTranslator>
#include <QJsonObject>
namespace aster::ui
{
// Catalogs are compiled resources; end users need no external translation files.
class LanguageCatalog : public QTranslator
{
  public:
    static LanguageCatalog &instance();
    bool load(const QString &language);
    QString label(const QString &key, const QString &fallback = {}) const;
    QString translate(const char *context, const char *source, const char *disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override
    {
        return m_current.isEmpty();
    }
    QString language() const
    {
        return m_language;
    }

  private:
    LanguageCatalog();
    QJsonObject m_current, m_english;
    QString m_language = "zh";
};
} // namespace aster::ui
#endif
