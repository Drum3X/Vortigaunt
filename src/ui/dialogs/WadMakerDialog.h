#pragma once

#include <QDialog>
#include <QListWidget>
#include <QString>
#include <QStringList>

class QAction;
class QLineEdit;
class QLabel;
class QSplitter;
class WadArchive;

/**
 * @brief Enhanced WAD Editor dialog with thumbnail grid view
 * 
 * Features:
 * - Open/Edit existing WAD files
 * - Create new WAD from images
 * - Thumbnail grid view like Hammer Editor
 * - Preview panel for selected texture
 * - Rename textures inline
 * - GoldSrc limit compliance (15 char names, 512 max size, 4096 textures)
 */
class WadMakerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WadMakerDialog(QWidget* parent = nullptr);
    ~WadMakerDialog();
    
    // Load WAD file directly (for command-line file association)
    bool loadWad(const QString& filePath);

    // the commands, for the ribbon; enabled while they can be used
    QAction* newAction() const { return m_newAction; }
    QAction* openAction() const { return m_openAction; }
    QAction* saveAction() const { return m_saveAction; }
    QAction* saveAsAction() const { return m_saveAsAction; }
    QAction* addImagesAction() const { return m_addAction; }
    QAction* removeAction() const { return m_removeAction; }
    QAction* extractAction() const { return m_extractAction; }
    QAction* renameAction() const { return m_renameAction; }

private slots:
    // File operations
    void onNewWad();
    void onOpenWad();
    void onSaveWad();
    void onSaveWadAs();

    // Texture operations  
    void onAddImages();
    void onRemoveSelected();
    void onExtractSelected();
    void onRenameSelected();

    // UI updates
    void onSelectionChanged();
    void onFilterChanged(const QString& text);
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUi();
    void refreshTextureList();
    void updateTextureCount();
    void updatePreview(int index);
    void populateThumbnails();
    bool maybeSave(); // Check if user wants to save before closing

    // UI elements
    QListWidget* m_textureGrid;
    QLabel* m_previewLabel;
    QLabel* m_previewInfo;
    QLabel* m_statusLabel;
    QLineEdit* m_filterEdit;
    
    // Commands, shown in the ribbon
    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_addAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_extractAction = nullptr;
    QAction* m_renameAction = nullptr;
    
    // WAD data
    WadArchive* m_wadMaker;
    QString m_currentFilePath;
    bool m_modified;
    
    // Constants
    static constexpr int THUMBNAIL_SIZE = 64;
    static constexpr int MAX_TEXTURES = 4096;
    static constexpr int MAX_NAME_LENGTH = 15;
};
