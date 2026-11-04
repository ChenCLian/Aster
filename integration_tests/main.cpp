#include "integration_suites.h"
#include <QApplication>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int status = runWorkspaceServicesTests(argc, argv);
    status |= runInteractionSafetyTests(argc, argv);
    status |= runRecordDirectoryTests(argc, argv);
    status |= runDatabaseTransferTests(argc, argv);
    return status;
}
