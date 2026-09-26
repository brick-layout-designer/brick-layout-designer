#pragma once

#include <QByteArray>
#include <QFile>

#include <memory>
#include <QHash>
#include <QString>
#include <QStringList>

namespace bld::import {

// Read-only access to the contents of a `.lif` archive (LEGO Digital
// Designer's packed asset bundle). LDD's `Assets.lif` contains every
// part geometry file, every material, every decoration as a single
// 100+ MB packed blob; nothing useful gets done with LDD models
// without first being able to extract files from it.
//
// Format: 84-byte file header starting with "LIFF" magic, big-endian
// 32-bit fields. The table of contents starts at `uint32_be(at_offset_72) + 64`
// and nests directories that ultimately point at packed file payloads.
// Names are UTF-16BE.
//
// Port of JrMasterModelBuilder/LIF-Extractor (GPL-3.0). Same algorithm,
// rewritten in idiomatic C++/Qt to match the rest of bld_import. The
// public surface here intentionally exposes "lookup-by-path" and
// "extract-to-bytes" rather than the python "extract-everything-to-
// disk" mode — for our use case we only need a handful of files at a
// time (the model's referenced part .g files).
//
// Usage:
//   LifReader r;
//   if (!r.open(QStringLiteral("/path/to/Assets.lif"))) { ... }
//   const QStringList files = r.fileList();   // every "/path/inside.lif"
//   QByteArray bytes = r.read("/Primitives/LOD0/3001.g");
class LifReader {
public:
    LifReader() = default;
    ~LifReader() = default;
    LifReader(const LifReader&) = delete;
    LifReader& operator=(const LifReader&) = delete;

    // Open `path`. Returns false if the file cannot be read or the
    // header magic doesn't match. errorString() carries a diagnostic.
    // The file is memory-mapped where possible (LDD's Assets.lif is
    // ~500 MB), falling back to reading it into memory.
    bool open(const QString& path);

    // Open the LIF archive stored as `lifPath` inside `outer` (LDD ships
    // its brick database as /db.lif inside Assets.lif). No copy is made:
    // `outer` must stay open and alive while this reader is used.
    bool openNested(const LifReader& outer, const QString& lifPath);
    bool isOpen() const { return !data_.isEmpty(); }
    const QString& errorString() const { return errorString_; }

    // Every file (not directory) the archive contains, as forward-
    // slash-separated paths. The leading "/" is included to match the
    // raw LIF naming convention.
    QStringList fileList() const;

    // True iff `lifPath` exists as a file inside the archive.
    bool contains(const QString& lifPath) const { return entries_.contains(lifPath); }

    // Read a file's bytes by its in-archive path. Returns an empty
    // QByteArray if the path is missing or refers to a directory.
    QByteArray read(const QString& lifPath) const;

    // Extract every file to disk under `destRoot`, preserving the
    // archive's directory tree. Returns the number of files written;
    // appends to errorString() on per-file failures but continues.
    int extractAll(const QString& destRoot);

private:
    struct FileEntry {
        qint64 offset = 0;
        qint64 size   = 0;
    };

    QString             errorString_;
    std::unique_ptr<QFile> file_;   // owns the mapping behind data_
    QByteArray          data_;      // whole archive (may alias file_'s map)
    QHash<QString, FileEntry> entries_;  // keyed on "/path/in/archive"

    bool parseTableOfContents();
    bool parseArchive();
};

}  // namespace bld::import
