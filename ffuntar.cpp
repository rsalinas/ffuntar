/*
 * ffuntar - Flash-friendly untar
 *
 * Written by Raul Salinas-Monteagudo <rausalinas@gmail.com> 2016-03
 *
 * Based on an example of libarchive
 *  https://github.com/libarchive/libarchive/blob/master/examples/untar.c
 */

#include <cerrno>
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <cassert>
#include <QDir>

#include <QCommandLineParser>
#include <QDebug>

class Ffuntar {
public:
    Ffuntar(const char *filename, int flags, bool verbose,
            const QString &otherDir)
        : verbose(verbose)
        , otherDir(otherDir)
    {
        archive_write_disk_set_options(ext, flags);
        /*
         * Note: archive_write_disk_set_standard_lookup() is useful
         * here, but it requires library routines that can add 500k or
         * more to a static executable.
         */
        //    archive_read_disk_set_standard_lookup(a);
        archive_read_support_format_tar(a);
        //    archive_read_support_format_zip(a);
        //    archive_read_support_format_raw(a);
        /*
         * On my system, enabling other archive formats adds 20k-30k
         * each.  Enabling gzip decompression adds about 20k.
         * Enabling bzip2 is more expensive because the libbz2 library
         * isn't very well factored.
         */
        if (filename != NULL && strcmp(filename, "-") == 0)
            filename = NULL;
        int r;
        if ((r = archive_read_open_filename(a, filename, 10240)))
            throw LibarchiveException("archive_read_open_filename()", a);
    }

    void setStripLevel(int n) {
        m_stripLevels = n;
    }

    int extract(bool do_extract) {
        for (;;) {
            struct archive_entry *entry;
            int r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r != ARCHIVE_OK)
                throw LibarchiveException("archive_read_next_header()", a);
            if (verbose && do_extract)
                qDebug() << "extract";
            if (verbose || !do_extract)
                qDebug() << archive_entry_pathname(entry);
            if (do_extract) {
                if (extract(entry) < 0) {
                    qWarning() << "There were errors.";
                    return EXIT_FAILURE;
                }
            }
            if (verbose || !do_extract)
                qDebug() << "\n";
        }


        return EXIT_SUCCESS;
    }

    void showStats() const {
        qDebug() << "Finished. Savings: "<< savedWriteBytes
                 << "written bytes in " << linkedFiles<< "files"
                 << float(savedWriteBytes)*100/totalBytes << "%";
    }


    int normalCopy(struct archive_entry *entry) {
        int r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            qWarning() << "archive_write_header()", archive_error_string(ext);
        } else {
            //        qDebug() << "simply copying data" ;
            copy_data(archive_entry_pathname(entry));
            int r = archive_write_finish_entry(ext);
            if (r != ARCHIVE_OK)
                throw LibarchiveException("archive_write_finish_entry()", ext);
        }
        return 0;
    }

    class FdGuard {
    public:
        FdGuard(int fd) : m_fd(fd) {}
        ~FdGuard() { close(m_fd); }
    private:
        int m_fd;
    };

    int copyAll(int from, int to, size_t sz) {
        assert(sz > 0 );
        size_t pending = sz;
        char buffer[65536];
        while (pending > 0) {
            int x = read(from, buffer, std::min(sizeof(buffer), pending)) ;
            if (x<0) {
                qWarning() << "Error re-reading file";
                return -1;
            }
            int y = write(to, buffer, x);
            if (y < x) {
                qWarning() << "Could not write everything";
                return -1;
            }
            pending -= x;
        }

    }
    int copyRest(struct archive_entry *entry,
                 size_t bytesCurrentBuffer,
                 int refFd,
                 char *buffer,
                 size_t equalBytes,
                 const struct stat &st) {

        int r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            qWarning() << "archive_write_header()", archive_error_string(ext);
        }
        lseek(refFd, 0, SEEK_SET);

        unlink(archive_entry_pathname(entry));
        int fdnew = open(archive_entry_pathname(entry), O_WRONLY | O_TRUNC | O_CREAT, st.st_mode);
        if (fdnew < 0) {
            perror("open fdnew");
            return -1;
        }
        FdGuard fdnewg(fdnew);
        if (equalBytes)
            copyAll(refFd, fdnew, equalBytes);

        //Write the current buffer
        if (write(fdnew, buffer, bytesCurrentBuffer) != bytesCurrentBuffer) {
            perror("write to new file");
            return -1;
        }

        //Extract the rest of the file
        int n2;
        do {
            n2 = archive_read_data(a, buffer, sizeof(buffer));
            if (n2 <0) {
                qWarning() << "error reading data in last phase";
                return -1;
            }
            ssize_t written = write(fdnew, buffer, n2);
            if (written  != n2) {
                qWarning() << "error writing rest of file after this buffer" << written  << n2;
                return -1;
            }
        } while (n2 != 0);

        int rr = archive_write_finish_entry(ext);
        if (rr != ARCHIVE_OK)
            throw LibarchiveException("archive_write_finish_entry()",ext);
        return 0;

    }

    int linkEqualFiles(struct archive_entry *entry,
                       const char * fn,
                       const QString &other,
                       const struct stat &st) {
        if (verbose)
            qDebug() << "link" <<fn;

        linkedFiles++;
        savedWriteBytes+=st.st_size;
        if (link(other.toStdString().c_str(), archive_entry_pathname(entry)) < 0) {
            if (errno == EEXIST) {
                // not serious: it already existed
                unlink(archive_entry_pathname(entry));
                if (link(other.toStdString().c_str(), fn) < 0) {
                    // cannot ln after unlink
                    return -1;
                }
            } else {
                qWarning() << "error linking" << strerror(errno);
                return -1;
            }
        }

        if (archive_write_finish_entry(ext) != ARCHIVE_OK)
            throw LibarchiveException("archive_write_finish_entry()", ext);
        return 0;
    }

    QString stripFile(const QString &fn) {
        QString ret = fn;
        for (int i = 0; i < m_stripLevels; ++i) {
            static const QRegExp re("^[^/]*/");
            ret = ret.replace(re, "");
        }
        if (m_stripLevels && verbose)
            qDebug() << fn << "->" << ret;
        return ret;
    }

    int extract(struct archive_entry *entry)
    {
        // FIXME: Clean this very quick and dirty implementation

        const char * fn = archive_entry_pathname(entry);
        const struct stat &st = *archive_entry_stat(entry);
        //    qDebug() << fn;
        totalBytes+=st.st_size;
        if (!S_ISREG(st.st_mode)) {
            if (verbose)
                qDebug() << "extract " << fn;
            return normalCopy(entry);
        }
        char buffer[65536];
        QString other = otherDir.absoluteFilePath(stripFile(fn));

        // Try to open the reference file;
        int refFd = open(other.toStdString().c_str(), O_RDONLY);
        posix_fadvise64(refFd, 0, 0, POSIX_FADV_NOREUSE);

        if (refFd < 0) {
            // New file, cannot read reference
            normalCopy(entry);
            return 0;
        }
        FdGuard fdg(refFd);
        struct stat fd_stat;
        if (fstat(refFd, &fd_stat) < 0) {
            perror("fstat");
            return -1;
        }
        if (fd_stat.st_size != st.st_size) {
            // Reference size mismatch, copying
            return normalCopy(entry);
        }

        int equalBytes = 0;
        int n;
        do {
            n = archive_read_data(a, buffer, sizeof(buffer));
            if (n < 0 ) {
                qWarning() << "error reading" << archive_error_string(ext);
                return -1;
            }
            char referenceBuffer[sizeof(buffer)];
            int m = read(refFd, referenceBuffer, n);
            if (m < n || memcmp(buffer, referenceBuffer, m)) {
                //reference differs
                return copyRest(entry, n, refFd, buffer, equalBytes, st);
            }
            equalBytes+=n;
        } while (n);

        //EOF reached, and everything is equal :)
        return linkEqualFiles(entry, fn, other, st);
    }


    int copy_data(const char * fn)
    {
        int r;
        if (verbose)
            qDebug() << "fich " << fn;
        const void *buff;
        size_t size;
#if ARCHIVE_VERSION >= 3000000
        int64_t offset;
#else
        off_t offset;
#endif

        for (;;) {
            r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF)
                return (ARCHIVE_OK);
            if (r != ARCHIVE_OK)
                return (r);
            r = archive_write_data_block(ext, buff, size, offset);
            if (r != ARCHIVE_OK) {
                qWarning() << "archive_write_data_block()" <<
                              archive_error_string(ext);
                return (r);
            }
        }
    }

    ~Ffuntar() {
        archive_read_close(a);
        archive_read_free(a);
    }

    //    class FfuntarException : public std::exception {
    //    public:

    //    };

    class LibarchiveException : public std::exception {
    public:
        LibarchiveException(const QString &cause, struct archive *a)
            : m_message(QStringLiteral("Libarchive error: %1 - %2").arg(cause, archive_error_string(a)).toStdString())
        {
        }

        const char * what() const noexcept(true) override
        {
            return m_message.c_str();
        }
    private:
        std::string m_message;
    };


private:
    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    const QDir otherDir;
    int m_stripLevels = 0;

    size_t linkedFiles = 0;
    size_t savedWriteBytes = 0;
    size_t totalBytes = 0;
    const bool verbose;
};

int
main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int flags = ARCHIVE_EXTRACT_TIME;
    QCoreApplication::setApplicationName("fftar");
    QCoreApplication::setApplicationVersion("0.1");
    QCoreApplication::setOrganizationName("Raúl Salinas-Monteagudo");

    QCommandLineParser parser;
    parser.setApplicationDescription("Flash-friendly tar uncompressor");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption showProgressOption("d", QCoreApplication::translate("main", "Show progress during extraction"));
    parser.addOption(showProgressOption);

    QCommandLineOption preserveOption(QStringList() << "p" << "preserve",
                                      QCoreApplication::translate("main", "Preserve attributes."));
    parser.addOption(preserveOption);
    QCommandLineOption listOption(QStringList() << "t" << "list",
                                  QCoreApplication::translate("main", "Just list."));
    parser.addOption(listOption);

    // An option with a value
    QCommandLineOption referenceDirOption(QStringList() << "r" << "reference-directory",
                                          QCoreApplication::translate("main", "Link to files in this folder if they are equal."),
                                          QCoreApplication::translate("main", "directory"));
    parser.addOption(referenceDirOption);


    QCommandLineOption archiveNameOption(QStringList() << "f" << "filename",
                                         QCoreApplication::translate("main", "Input archive name."),
                                         QCoreApplication::translate("main", "filename"));
    parser.addOption(archiveNameOption);

    QCommandLineOption stripPrefixOption(QStringList() << "s" << "strip-prefix",
                                         QCoreApplication::translate("main", "Strips the given amount of prefix directories."),
                                         QCoreApplication::translate("main", "levels"));
    parser.addOption(stripPrefixOption);

    // Process the actual command line arguments given by the user
    parser.process(app);

    bool showProgress = parser.isSet(showProgressOption);

    if (parser.isSet(preserveOption)) {
        flags |= ARCHIVE_EXTRACT_PERM;
        flags |= ARCHIVE_EXTRACT_ACL;
        flags |= ARCHIVE_EXTRACT_FFLAGS;
    }

    QString filename;
    if (parser.isSet(archiveNameOption)) {
        filename = parser.value(archiveNameOption);
    }

    try {
        Ffuntar ffu(filename.size() ? filename.toStdString().c_str() : nullptr,
                    flags, showProgress, parser.value(referenceDirOption));
        ffu.setStripLevel(parser.value(stripPrefixOption).toInt());
        int ret = ffu.extract(! parser.isSet(listOption));
        ffu.showStats();
        return ret;
    } catch (const std::exception &e) {
        qWarning() << e.what();
        return EXIT_FAILURE;
    }
}
