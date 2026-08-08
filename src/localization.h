#pragma once

enum class Language {
    Chinese,
    English,
};

const char* SelectText(Language language, const char* chinese, const char* english);

struct UiStrings {
    const char* heading;
    const char* subtitle;
    const wchar_t* folderDialogTitle;
    const char* folderHint;
    const char* browse;
    const char* searchSubdirectories;
    const char* preserveExif;
    const char* deleteOriginals;
    const char* overwriteExisting;
    const char* outputFormat;
    const char* pngOption;
    const char* jpegOption;
    const char* pngCompressionFormat;
    const char* jpegQualityFormat;
    const char* parallelThreads;
    const char* workerCountFormat;
    const char* detectedProcessorsFormat;
    const char* start;
    const char* stopping;
    const char* cancel;
    const char* statusFormat;
    const char* summaryFormat;
    const char* currentFormat;
    const char* processingLog;
    const char* tip;
    const char* failurePrefix;
    const char* taskFailurePrefix;
    const char* skippedPrefix;
    const char* scanWarningPrefix;
};

const UiStrings& GetUiStrings(Language language);
