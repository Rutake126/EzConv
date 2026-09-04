function logToNative(tag, msg) {
    const formatted = typeof msg === 'object' ? JSON.stringify(msg) : String(msg);
    try {
        console.log(`[EzConv][${tag}] ${formatted}`);
        if (window.chrome && window.chrome.webview) {
            window.chrome.webview.postMessage(JSON.stringify({
                type: 'debug_log',
                tag: tag,
                msg: formatted
            }));
        }
    } catch (e) {}
}

window.onerror = function(message, source, lineno, colno, error) {
    logToNative('JS_ERROR', `${message} at ${source}:${lineno}:${colno}`);
};
window.onunhandledrejection = function(event) {
    logToNative('JS_PROMISE_ERROR', `${event.reason}`);
};

// Safe Storage fallback to memory to guard against about:blank WebView2 SecurityError
const _memoryStorage = new Map();
function safeStorageGet(key, defaultValue = null) {
    try {
        if (window.localStorage) {
            const v = window.localStorage.getItem(key);
            return v !== null ? v : defaultValue;
        }
    } catch (e) {
        logToNative('STORAGE', `safeStorageGet fallback to memory for "${key}": ${e.name}`);
    }
    return _memoryStorage.has(key) ? _memoryStorage.get(key) : defaultValue;
}

function safeStorageSet(key, value) {
    try {
        if (window.localStorage) {
            window.localStorage.setItem(key, value);
            return;
        }
    } catch (e) {
        logToNative('STORAGE', `safeStorageSet fallback to memory for "${key}": ${e.name}`);
    }
    _memoryStorage.set(key, String(value));
}

function safeStorageRemove(key) {
    try {
        if (window.localStorage) {
            window.localStorage.removeItem(key);
            return;
        }
    } catch (e) {
        logToNative('STORAGE', `safeStorageRemove fallback to memory for "${key}": ${e.name}`);
    }
    _memoryStorage.delete(key);
}

class JP2ConverterApp {
    constructor() {
        logToNative('INIT', 'JP2ConverterApp constructor START');
        this.tasks = [];
        this.isProcessing = false;
        this.selectedFormat = 'jpg';
        this.quality = 90;
        this.threads = 4;
        this.djvuMode = 'mrc';
        this.keepOcr = true;
        this.keepBookmarks = true;
        this.currentLang = 'zh';
        this.currentBookName = '未命名书籍';
        this.currentFolderPath = '';
        this.lastOutputDir = '';

        this.i18n = {
            zh: {
                title: "EzConv v.1.1.0",
                subtitle: "(基于 OpenJPEG 与 DjVuLibre 官方双内核)",
                linkOptions: "- 查看所有选项",
                linkAbout: "- 了解更多",
                placeholder: "在此处粘贴或输入 jp2 / djvu 文件的地址",
                btnOpen: "打开地址",
                dropzone: "将文件拖拽至此处或者单击手动选择",
                dropzoneSub: "支持单个/多个 .jp2 / .djvu 文件或扫描书籍文件夹",
                modalTitle: "选项",
                aboutTitle: "关于 EzConv",
                aboutDesc: "本工具用于将 JPEG 2000 (<code>.jp2</code>, <code>.j2k</code>) 与 DjVu (<code>.djvu</code>, <code>.djv</code>) 高清古籍/扫描图批量转换为通用的 JPG、PNG 或高保真 PDF 格式。",
                aboutF1: "纯本地 C++17 离线极速解码，零网络上传，安全隐私。",
                aboutF2: "基于 OpenJPEG 官方解码内核，支持 16-bit 自动下采样与色彩保真。",
                aboutF3: "基于 DjVuLibre 官方高保真内核，支持 CCITT G4 无损紧凑压缩与 PDF 高清直转。",
                aboutF4: "支持多核 CPU 并发多线程加速，专为大型古籍与扫描书籍优化。",
                aboutBtn: "知道了",
                startConvert: "开始转换",
                cancel: "取消",
                converting: "转换中...",
                downloadResult: "保存",
                convertAnother: "继续转换",
                backToHome: "❮",
                readyStatus: "准备转换",
                doneStatus: "✔ 转换完成！",
                doneToast: "转换完成！已保存在输出目录"
            },
            en: {
                title: "EzConv v.1.1.0",
                subtitle: "(Based on OpenJPEG & DjVuLibre Official Engines)",
                linkOptions: "- View Options",
                linkAbout: "- Learn More",
                placeholder: "Paste or enter jp2 / djvu file address here",
                btnOpen: "Open Path",
                dropzone: "Drag and drop files here, or click to choose manually",
                dropzoneSub: "Supports single/multiple .jp2 / .djvu files or folders",
                modalTitle: "Options",
                aboutTitle: "About EzConv",
                aboutDesc: "A lightweight desktop utility to batch convert JPEG 2000 (<code>.jp2</code>, <code>.j2k</code>) and DjVu (<code>.djvu</code>, <code>.djv</code>) scanned documents/books to standard JPG, PNG, or high-fidelity compact PDF formats.",
                aboutF1: "100% local C++17 offline processing with zero network upload and full privacy.",
                aboutF2: "Official OpenJPEG decoding engine with automatic 16-bit downsampling and color fidelity.",
                aboutF3: "Official DjVuLibre engine with CCITT Group 4 lossless compression and direct PDF generation.",
                aboutF4: "Multi-threaded CPU concurrency optimized for large scanned books and documents.",
                aboutBtn: "Got it",
                startConvert: "Start Conversion",
                cancel: "Cancel",
                converting: "Converting...",
                downloadResult: "Save",
                convertAnother: "Continue",
                backToHome: "❮",
                readyStatus: "Ready",
                doneStatus: "✔ Conversion Completed!",
                doneToast: "Conversion completed! Saved to output folder"
            }
        };

        this.initDOMElements();
        logToNative('INIT', 'initDOMElements OK');

        this.bindEvents();
        logToNative('INIT', 'bindEvents OK');

        // Test localStorage explicitly as requested
        try {
            logToNative('STORAGE', 'testing localStorage START');
            const storage = window.localStorage;
            logToNative('STORAGE', 'localStorage object: ' + (storage ? 'exists' : 'null/undefined'));
            const value = storage.getItem('__ezconv_test__');
            logToNative('STORAGE', 'getItem OK, value: ' + value);
            storage.setItem('__ezconv_test__', '1');
            storage.removeItem('__ezconv_test__');
            logToNative('STORAGE', 'read/write OK');
        } catch (e) {
            logToNative('STORAGE_ERROR', 'FAILED: ' + e.name + ' - ' + e.message);
        }

        this.initTheme();
        logToNative('INIT', 'initTheme OK');

        logToNative('INIT', 'JP2ConverterApp constructor END');
    }

    initDOMElements() {
        // Views
        this.homeView = document.getElementById('homeView');
        this.bookConvertView = document.getElementById('bookConvertView');
        this.btnBackToHome = document.getElementById('btnBackToHome');

        // Book View Details
        this.bookTitleText = document.getElementById('bookTitleText');
        this.bookMetaText = document.getElementById('bookMetaText');
        this.bookProgressStatus = document.getElementById('bookProgressStatus');
        this.bookProgressPercent = document.getElementById('bookProgressPercent');
        this.bookProgressBarFill = document.getElementById('bookProgressBarFill');
        this.btnCancelConvert = document.getElementById('btnCancelConvert');
        this.btnStartBookConvert = document.getElementById('btnStartBookConvert');
        this.btnDownloadBookResult = document.getElementById('btnDownloadBookResult');
        this.btnConvertAnotherBook = document.getElementById('btnConvertAnotherBook');

        // Theme & Modals
        this.themeToggleBtn = document.getElementById('themeToggleBtn');
        this.openSettingsBtn = document.getElementById('openSettingsBtn');
        this.linkViewOptions = document.getElementById('linkViewOptions');
        this.linkAbout = document.getElementById('linkAbout');
        this.settingsModal = document.getElementById('settingsModal');
        this.aboutModal = document.getElementById('aboutModal');
        this.closeSettingsModal = document.getElementById('closeSettingsModal');
        this.closeAboutModal = document.getElementById('closeAboutModal');
        this.closeAboutBtn = document.getElementById('closeAboutBtn');
        this.saveSettingsBtn = document.getElementById('saveSettingsBtn');

        // Settings Inputs (Modal)
        this.modalLangSelect = document.getElementById('modalLangSelect');
        this.themeSunBtn = document.getElementById('themeSunBtn');
        this.themeMoonBtn = document.getElementById('themeMoonBtn');
        this.settingFormat = document.getElementById('settingFormat');
        this.settingQuality = document.getElementById('settingQuality');
        this.qualityText = document.getElementById('qualityText');
        this.settingThreads = document.getElementById('settingThreads');
        this.threadsText = document.getElementById('threadsText');
        this.qualityRow = document.getElementById('qualityRow');

        this.djvuOptionsSection = document.getElementById('djvuOptionsSection');
        this.settingDjvuMode = document.getElementById('settingDjvuMode');
        this.settingKeepOcr = document.getElementById('settingKeepOcr');
        this.settingKeepBookmarks = document.getElementById('settingKeepBookmarks');

        // Path Input Bar (Home)
        this.pathInput = document.getElementById('pathInput');
        this.openPathBtn = document.getElementById('openPathBtn');

        // Dropzone & Native inputs (Home)
        this.dropzone = document.getElementById('dropzone');
        this.fileInput = document.getElementById('fileInput');
        this.folderInput = document.getElementById('folderInput');

        // Toast
        this.toastBox = document.getElementById('toastBox');
    }

    bindEvents() {
        const isNative = () => Boolean(window.chrome && window.chrome.webview);

        // Frameless Window Controls
        const btnWinMin = document.getElementById('btnWinMinimize');
        const btnWinMax = document.getElementById('btnWinMaximize');
        const btnWinClose = document.getElementById('btnWinClose');
        const iconMax = document.getElementById('iconMax');
        const iconRestore = document.getElementById('iconRestore');
        const titlebar = document.getElementById('windowTitlebar');

        if (btnWinMin) {
            btnWinMin.addEventListener('click', () => {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'window_minimize' }));
                }
            });
        }
        if (btnWinMax) {
            btnWinMax.addEventListener('click', () => {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'window_maximize' }));
                }
            });
        }
        if (btnWinClose) {
            btnWinClose.addEventListener('click', () => {
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'window_close' }));
                }
            });
        }
        if (titlebar) {
            titlebar.addEventListener('mousedown', (e) => {
                if (e.target.closest('.window-controls')) return;
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'window_drag' }));
                }
            });
            titlebar.addEventListener('dblclick', (e) => {
                if (e.target.closest('.window-controls')) return;
                if (window.chrome && window.chrome.webview) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'window_maximize' }));
                }
            });
        }

        window.onWindowMaximizedChanged = (isMaximized) => {
            if (iconMax && iconRestore && btnWinMax) {
                iconMax.style.display = isMaximized ? 'none' : 'block';
                iconRestore.style.display = isMaximized ? 'block' : 'none';
                btnWinMax.title = isMaximized ? '恢复' : '放大';
            }
        };

        // Theme Toggle (if exists)
        if (this.themeToggleBtn) {
            this.themeToggleBtn.addEventListener('click', () => this.toggleTheme());
        }

        // Theme Buttons (in Modal)
        if (this.themeSunBtn) {
            this.themeSunBtn.addEventListener('click', () => this.setTheme('light'));
        }
        if (this.themeMoonBtn) {
            this.themeMoonBtn.addEventListener('click', () => this.setTheme('dark'));
        }

        // Language items (Navbar)
        document.querySelectorAll('.lang-item').forEach(item => {
            item.addEventListener('click', (e) => {
                document.querySelectorAll('.lang-item').forEach(el => el.classList.remove('active'));
                item.classList.add('active');
                const lang = item.getAttribute('data-lang');
                this.setLanguage(lang);
                if (this.modalLangSelect) this.modalLangSelect.value = lang;
            });
        });

        // Modal Language Select
        if (this.modalLangSelect) {
            this.modalLangSelect.addEventListener('change', (e) => {
                const lang = e.target.value;
                this.setLanguage(lang);
                document.querySelectorAll('.lang-item').forEach(el => {
                    el.classList.toggle('active', el.getAttribute('data-lang') === lang);
                });
            });
        }

        // Modals Open / Close
        const openSettings = () => {
            this.settingsModal.style.display = 'flex';
        };
        if (this.openSettingsBtn) {
            this.openSettingsBtn.addEventListener('click', openSettings);
        }
        if (this.linkViewOptions) {
            this.linkViewOptions.addEventListener('click', openSettings);
        }

        this.closeSettingsModal.addEventListener('click', () => {
            this.settingsModal.style.display = 'none';
        });

        this.saveSettingsBtn.addEventListener('click', () => {
            this.selectedFormat = this.settingFormat.value;
            let q = parseInt(this.settingQuality.value);
            if (isNaN(q) || q < 1) q = 90;
            if (q > 100) q = 100;
            this.quality = q;

            let th = parseInt(this.settingThreads.value);
            if (isNaN(th) || th < 1) th = 4;
            if (th > 64) th = 64;
            this.threads = th;

            if (this.settingDjvuMode) this.djvuMode = this.settingDjvuMode.value;
            if (this.settingKeepOcr) this.keepOcr = this.settingKeepOcr.checked;
            if (this.settingKeepBookmarks) this.keepBookmarks = this.settingKeepBookmarks.checked;

            this.settingsModal.style.display = 'none';
            this.updateBookMeta();
            this.showToast('设置已保存');
        });

        this.linkAbout.addEventListener('click', () => {
            this.aboutModal.style.display = 'flex';
        });

        this.closeAboutModal.addEventListener('click', () => {
            this.aboutModal.style.display = 'none';
        });
        this.closeAboutBtn.addEventListener('click', () => {
            this.aboutModal.style.display = 'none';
        });

        // Settings Inputs initialization
        this.settingQuality.value = this.quality;
        this.settingThreads.value = this.threads;

        this.settingFormat.addEventListener('change', (e) => {
            const val = e.target.value;
            this.selectedFormat = val;
            this.qualityRow.style.display = (val === 'jpg' || val === 'pdf') ? 'flex' : 'none';
            if (this.djvuOptionsSection) {
                this.djvuOptionsSection.style.display = (val === 'pdf') ? 'block' : 'none';
            }
        });

        // Dropzone click -> Native Files Picker (Supports single/multiple .jp2 files)
        this.dropzone.addEventListener('click', (e) => {
            logToNative('UI', 'dropzone clicked, isNative=' + isNative());
            if (e.target === this.fileInput || e.target === this.folderInput) return;
            if (isNative()) {
                logToNative('UI', 'posting select_files message to C++');
                window.chrome.webview.postMessage(JSON.stringify({ type: 'select_files' }));
            } else {
                this.fileInput.click();
            }
        });

        this.fileInput.addEventListener('change', (e) => this.handleFileSelect(e.target.files));
        this.folderInput.addEventListener('change', (e) => this.handleFileSelect(e.target.files));

        // Drag & Drop
        // Global Drag & Drop handling to prevent WebView2 navigation
        ['dragenter', 'dragover'].forEach(name => {
            window.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
            });
            this.dropzone.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.dropzone.classList.add('drag-over');
            });
        });

        ['dragleave', 'drop'].forEach(name => {
            window.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
            });
            this.dropzone.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.dropzone.classList.remove('drag-over');
            });
        });

        this.dropzone.addEventListener('drop', (e) => this.handleDrop(e));
        window.addEventListener('drop', (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (this.homeView && this.homeView.style.display !== 'none') {
                this.handleDrop(e);
            }
        });

        // Address bar open button
        const handleOpenAddress = () => {
            const rawPath = this.pathInput.value.trim();
            if (!rawPath) {
                if (isNative()) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'select_files' }));
                } else {
                    this.fileInput.click();
                }
                return;
            }
            if (isNative()) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    type: 'select_path_input', 
                    path: rawPath 
                }));
            } else {
                this.showToast('请直接点击上方选择文件或输入路径');
            }
        };

        this.openPathBtn.addEventListener('click', handleOpenAddress);
        this.pathInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') handleOpenAddress();
        });

        // Book View Navigation & Actions
        if (this.btnBackToHome) {
            this.btnBackToHome.addEventListener('click', () => this.showHomeView());
        }
        if (this.btnCancelConvert) {
            this.btnCancelConvert.addEventListener('click', () => this.showHomeView());
        }
        if (this.btnConvertAnotherBook) {
            this.btnConvertAnotherBook.addEventListener('click', () => this.showHomeView());
        }
        if (this.btnStartBookConvert) {
            this.btnStartBookConvert.addEventListener('click', () => this.startBatchConversion());
        }
        
        if (this.btnDownloadBookResult) {
            this.btnDownloadBookResult.addEventListener('click', () => {
                if (isNative()) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'save_converted_files' }));
                } else {
                    this.showToast('转换完成！图片已保存在输出目录中');
                }
            });
        }
    }

    setLanguage(lang) {
        this.currentLang = (lang === 'zh' || lang === 'en') ? lang : 'en';
        const t = this.i18n[this.currentLang] || this.i18n.en;
        document.getElementById('txtTitle').textContent = t.title;
        document.getElementById('txtSubtitle').textContent = t.subtitle;
        document.getElementById('txtLinkOptions').textContent = t.linkOptions;
        document.getElementById('txtLinkAbout').textContent = t.linkAbout;
        this.pathInput.placeholder = t.placeholder;
        this.openPathBtn.textContent = t.btnOpen;
        document.getElementById('txtDropzone').textContent = t.dropzone;
        document.getElementById('txtDropzoneSub').textContent = t.dropzoneSub;
        document.getElementById('txtModalOptionsTitle').textContent = t.modalTitle;
        const txtAboutTitle = document.getElementById('txtAboutTitle');
        if (txtAboutTitle && t.aboutTitle) txtAboutTitle.textContent = t.aboutTitle;
        const txtAboutDesc = document.getElementById('txtAboutDesc');
        if (txtAboutDesc && t.aboutDesc) txtAboutDesc.innerHTML = t.aboutDesc;
        const txtAboutFeatures = document.getElementById('txtAboutFeatures');
        if (txtAboutFeatures && t.aboutF1) {
            txtAboutFeatures.innerHTML = `
                <li>${t.aboutF1}</li>
                <li>${t.aboutF2}</li>
                <li>${t.aboutF3}</li>
                <li>${t.aboutF4}</li>
            `;
        }
        const closeAboutBtn = document.getElementById('closeAboutBtn');
        if (closeAboutBtn && t.aboutBtn) closeAboutBtn.textContent = t.aboutBtn;
        if (this.btnBackToHome) {
            this.btnBackToHome.textContent = t.backToHome;
        }
        if (this.btnCancelConvert) {
            this.btnCancelConvert.textContent = t.cancel;
        }
        if (!this.isProcessing) {
            this.btnStartBookConvert.textContent = t.startConvert;
        }
        this.btnDownloadBookResult.textContent = t.downloadResult;
        this.btnConvertAnotherBook.textContent = t.convertAnother;
        this.updateBookMeta();
    }

    initTheme() {
        logToNative('THEME', 'initTheme START');
        logToNative('THEME', 'before safeStorageGet');
        const savedTheme = safeStorageGet('app-theme') || 
            (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
        logToNative('THEME', 'after safeStorageGet, savedTheme = ' + savedTheme);
        this.setTheme(savedTheme);
        logToNative('THEME', 'initTheme END');
    }

    setTheme(theme) {
        const t = (theme === 'dark') ? 'dark' : 'light';
        document.documentElement.setAttribute('data-theme', t);
        safeStorageSet('app-theme', t);
        if (this.themeSunBtn) this.themeSunBtn.classList.toggle('active', t === 'light');
        if (this.themeMoonBtn) this.themeMoonBtn.classList.toggle('active', t === 'dark');
    }

    toggleTheme() {
        const currentTheme = document.documentElement.getAttribute('data-theme') || 'light';
        const nextTheme = currentTheme === 'dark' ? 'light' : 'dark';
        this.setTheme(nextTheme);
    }

    isSupportedFile(filename) {
        if (!filename) return false;
        return /\.(jp2|j2k|jpf|jpc|djvu|djv)$/i.test(filename);
    }

    async handleDrop(e) {
        if (e) {
            e.preventDefault();
            e.stopPropagation();
        }

        const isNativeEnv = Boolean(window.chrome && window.chrome.webview);
        const dtFiles = e.dataTransfer ? e.dataTransfer.files : null;

        // In native WebView2 app, extract actual Windows file paths from DataTransfer
        if (isNativeEnv && dtFiles && dtFiles.length > 0) {
            const paths = [];
            for (let i = 0; i < dtFiles.length; i++) {
                const f = dtFiles[i];
                if (f && f.path) {
                    paths.push(f.path);
                }
            }
            if (paths.length > 0) {
                window.chrome.webview.postMessage(JSON.stringify({
                    type: 'paths_dropped',
                    paths: paths
                }));
                return;
            }
        }

        const items = e.dataTransfer ? e.dataTransfer.items : null;
        if (items) {
            const files = [];
            for (let i = 0; i < items.length; i++) {
                const entry = items[i].webkitGetAsEntry ? items[i].webkitGetAsEntry() : null;
                if (entry) {
                    await this.traverseFileTree(entry, files);
                } else {
                    const file = items[i].getAsFile();
                    if (file && this.isSupportedFile(file.name)) files.push(file);
                }
            }
            this.addFiles(files);
        } else if (dtFiles) {
            this.handleFileSelect(dtFiles);
        }
    }

    async traverseFileTree(item, fileList) {
        if (item.isFile) {
            return new Promise((resolve) => {
                item.file((file) => {
                    if (this.isSupportedFile(file.name)) fileList.push(file);
                    resolve();
                });
            });
        } else if (item.isDirectory) {
            const dirReader = item.createReader();
            const entries = await new Promise((resolve) => dirReader.readEntries(resolve));
            for (const entry of entries) {
                await this.traverseFileTree(entry, fileList);
            }
        }
    }

    handleFileSelect(fileList) {
        const validFiles = Array.from(fileList).filter(f => this.isSupportedFile(f.name));
        if (validFiles.length === 0) {
            this.showToast('未检测到支持的 .jp2 或 .djvu 文件');
            return;
        }
        this.addFiles(validFiles);
    }

    extractTargetName(folderPath, files) {
        if (folderPath && folderPath.trim()) {
            const clean = folderPath.trim().replace(/[\/\\]+$/, '');
            const parts = clean.split(/[\/\\]/);
            if (parts.length > 0 && parts[parts.length - 1]) {
                return parts[parts.length - 1];
            }
        }
        if (files && files.length > 0) {
            if (files.length === 1) {
                return files[0].name;
            }
            const first = files[0];
            const p = first.path || first.name;
            if (p.includes('\\') || p.includes('/')) {
                const clean = p.replace(/[\/\\][^\/\\]+$/, '');
                const parts = clean.split(/[\/\\]/);
                if (parts.length > 0 && parts[parts.length - 1]) {
                    return parts[parts.length - 1];
                }
            }
            return `${files[0].name} 等 ${files.length} 个文件`;
        }
        return '未命名文件';
    }

    addFiles(files, customFolder) {
        this.tasks = [];
        const basePath = customFolder || this.pathInput.value.trim();

        files.forEach(file => {
            const filePath = file.path || file.name;
            this.tasks.push({
                id: 'task_' + Math.random().toString(36).substr(2, 9),
                file: {
                    name: file.name,
                    size: file.size,
                    path: filePath,
                    pages: file.pages || 0
                },
                status: 'queued',
                error: null
            });
        });

        // 智能检测是否包含 DjVu 文件，若包含则自动切换目标格式为 PDF
        const hasDjVu = files.some(f => {
            const n = (f.name || '').toLowerCase();
            return n.endsWith('.djvu') || n.endsWith('.djv');
        });

        if (hasDjVu) {
            this.selectedFormat = 'pdf';
            if (this.settingFormat) this.settingFormat.value = 'pdf';
            if (this.djvuOptionsSection) this.djvuOptionsSection.style.display = 'block';
            if (this.qualityRow) this.qualityRow.style.display = 'flex';
        }

        if (this.tasks.length > 0) {
            const targetName = this.extractTargetName(basePath, files);
            this.showBookConvertView(targetName, basePath);
            this.showToast(`已加载: ${targetName} (${this.tasks.length} 项)`);
        }
    }

    showHomeView() {
        if (this.isProcessing) return;
        this.homeView.style.display = 'flex';
        this.bookConvertView.style.display = 'none';
        this.tasks = [];
        this.pathInput.value = '';
    }

    showBookConvertView(targetName, folderPath) {
        this.homeView.style.display = 'none';
        this.bookConvertView.style.display = 'block';

        this.currentBookName = targetName;
        this.currentFolderPath = folderPath;
        this.bookTitleText.textContent = targetName;

        this.updateBookMeta();

        const t = this.i18n[this.currentLang] || this.i18n.en;
        this.bookProgressStatus.textContent = t.readyStatus;
        this.bookProgressPercent.textContent = `0% (0/${this.tasks.length})`;
        this.bookProgressBarFill.style.width = '0%';

        if (this.btnCancelConvert) {
            this.btnCancelConvert.style.display = 'inline-flex';
            this.btnCancelConvert.disabled = false;
        }

        this.btnStartBookConvert.style.display = 'inline-flex';
        this.btnStartBookConvert.disabled = false;
        this.btnStartBookConvert.textContent = t.startConvert;

        this.btnDownloadBookResult.style.display = 'none';
        this.btnConvertAnotherBook.style.display = 'none';
    }

    updateBookMeta() {
        if (!this.bookMetaText) return;
        const fmt = this.selectedFormat.toUpperCase();
        const qualityStr = (this.selectedFormat === 'jpg') ? ` (质量 ${this.quality})` : '';
        const langIsZh = this.currentLang === 'zh';
        
        let totalPages = 0;
        this.tasks.forEach(t => {
            totalPages += (t.file && t.file.pages && t.file.pages > 0) ? t.file.pages : 1;
        });

        if (langIsZh) {
            this.bookMetaText.textContent = `共 ${totalPages} 页 · 目标格式: ${fmt}${qualityStr} · 并发线程: ${this.threads}`;
        } else {
            this.bookMetaText.textContent = `Total: ${totalPages} pages · Format: ${fmt}${qualityStr} · Threads: ${this.threads}`;
        }
    }

    updateProgress(curr, total, filename) {
        const totalCount = total || this.tasks.length;
        const pct = totalCount > 0 ? Math.min(100, Math.round((curr / totalCount) * 100)) : 0;
        
        if (this.bookProgressBarFill) {
            this.bookProgressBarFill.style.width = `${pct}%`;
        }
        if (this.bookProgressPercent) {
            this.bookProgressPercent.textContent = `${pct}% (${curr}/${totalCount})`;
        }
        if (this.bookProgressStatus && filename) {
            this.bookProgressStatus.textContent = `正在转换: ${filename}`;
        }
    }

    async startBatchConversion() {
        if (this.tasks.length === 0 || this.isProcessing) return;

        const t = this.i18n[this.currentLang] || this.i18n.en;
        this.isProcessing = true;
        if (this.btnCancelConvert) {
            this.btnCancelConvert.disabled = true;
        }
        this.btnStartBookConvert.disabled = true;
        this.btnStartBookConvert.textContent = t.converting;
        this.bookProgressBarFill.style.width = "0%";
        this.bookProgressStatus.textContent = (this.currentLang === 'zh') ? "正在极速转换中..." : "Converting in progress...";
        
        // Reset tasks
        this.tasks.forEach(task => {
            task.status = 'queued';
            task.error = null;
        });

        // Check if running in Native C++ WebView2 Environment
        if (window.chrome && window.chrome.webview) {
            const basePath = this.currentFolderPath || this.pathInput.value.trim();
            const taskPayload = this.tasks.map(task => {
                let fullPath = task.file.path || task.file.name;
                if (!fullPath.includes('\\') && !fullPath.includes('/') && basePath) {
                    if (basePath.endsWith(task.file.name) || basePath.toLowerCase().endsWith(task.file.name.toLowerCase())) {
                        fullPath = basePath;
                    } else {
                        fullPath = basePath.replace(/[\/\\]+$/, '') + '\\' + task.file.name;
                    }
                }
                return {
                    id: task.id,
                    path: fullPath,
                    name: task.file.name
                };
            });
            const paths = taskPayload.map(task => task.path).filter(Boolean);

            window.chrome.webview.postMessage(JSON.stringify({
                type: 'start_convert',
                tasks: taskPayload,
                paths: paths,
                format: this.selectedFormat,
                quality: this.quality.toString(),
                threads: this.threads.toString(),
                djvu_mode: this.djvuMode,
                keep_ocr: this.keepOcr ? "true" : "false",
                keep_bookmarks: this.keepBookmarks ? "true" : "false"
            }));
            return;
        }

        // Browser Fallback Simulation
        for (let i = 0; i < this.tasks.length; i++) {
            const task = this.tasks[i];
            task.status = 'processing';
            this.updateProgress(i + 1, this.tasks.length, task.file.name);
            await new Promise(r => setTimeout(r, Math.max(80, Math.random() * 250)));
            task.status = 'success';
        }

        this.isProcessing = false;
        this.bookProgressBarFill.style.width = '100%';
        this.bookProgressPercent.textContent = `100% (${this.tasks.length}/${this.tasks.length})`;
        this.bookProgressStatus.textContent = t.doneStatus;
        if (this.btnCancelConvert) this.btnCancelConvert.style.display = 'none';
        this.btnStartBookConvert.style.display = 'none';
        this.btnDownloadBookResult.style.display = 'inline-flex';
        this.btnConvertAnotherBook.style.display = 'inline-flex';
        this.showToast(t.doneToast);
    }

    showToast(msg) {
        this.toastBox.textContent = msg;
        this.toastBox.style.display = 'block';
        setTimeout(() => {
            this.toastBox.style.display = 'none';
        }, 3500);
    }
}

let app;
function initApp() {
    logToNative('INIT', 'initApp entered');
    logToNative('INIT', 'window.app before: ' + (window.app ? 'valid' : 'undefined'));
    if (!window.app) {
        try {
            app = new JP2ConverterApp();
            window.app = app;
            logToNative('INIT', 'window.app created: ' + (window.app ? 'valid' : 'null'));
            logToNative('INIT', 'window.app instanceof JP2ConverterApp: ' + (window.app instanceof JP2ConverterApp));
        } catch (err) {
            logToNative('INIT_ERROR', 'new JP2ConverterApp threw: ' + err.name + ': ' + err.message + '\n' + (err.stack || ''));
        }
    }
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initApp);
} else {
    initApp();
}

// Global Native Callbacks called from C++ Core
window.onNativeFilesSelected = (files) => {
    logToNative('FILE', 'onNativeFilesSelected called');
    logToNative('FILE', 'files: ' + JSON.stringify(files));
    logToNative('APP', 'window.app: ' + (window.app ? 'valid' : 'undefined'));
    const inst = window.app || app;
    logToNative('APP', 'inst: ' + (inst ? 'valid' : 'undefined'));
    if (inst && files && files.length > 0) {
        logToNative('FILE', 'Calling inst.addFiles with ' + files.length + ' files');
        inst.addFiles(files.map(f => ({ name: f.name, size: f.size, path: f.path, pages: f.pages })));
    } else {
        logToNative('FILE_ERROR', 'onNativeFilesSelected skipped! inst=' + Boolean(inst) + ', files=' + Boolean(files && files.length));
    }
};

window.onNativeFolderSelected = (folder, files) => {
    const inst = window.app || app;
    if (inst && files) {
        if (inst.pathInput) inst.pathInput.value = folder;
        inst.addFiles(files.map(f => ({ name: f.name, size: f.size, path: f.path, pages: f.pages })), folder);
    }
};

window.onNativeProgress = function() {
    const inst = window.app || app;
    if (!inst) return;
    
    let taskId = null;
    let success = false;
    let errorMsg = '';
    let curr = 0;
    let total = 0;
    let filename = '';

    if (typeof arguments[0] === 'string' && arguments.length >= 4) {
        taskId = arguments[0];
        success = Boolean(arguments[1]);
        errorMsg = arguments[2] || '';
        curr = Number(arguments[3]) || 0;
        total = Number(arguments[4]) || 0;
        filename = arguments[5] || '';
    } else {
        curr = Number(arguments[0]) || 0;
        total = Number(arguments[1]) || 0;
        filename = arguments[2] || '';
        success = Boolean(arguments[3]);
        errorMsg = arguments[4] || '';
    }

    let task = null;
    if (taskId) {
        task = inst.tasks.find(t => t.id === taskId);
    }
    if (!task && filename) {
        task = inst.tasks.find(t => t.file.name === filename || (t.file.path && t.file.path.endsWith(filename)));
    }
    if (!task && curr > 0 && curr <= inst.tasks.length) {
        task = inst.tasks[curr - 1];
    }

    if (task) {
        task.status = success ? 'success' : 'error';
        task.error = errorMsg;
    }

    inst.updateProgress(curr, total, filename);
};

window.onNativePageProgress = function(taskId, curPage, totalPages, phase) {
    const inst = window.app || app;
    if (!inst) return;
    if (inst.bookProgressStatus) {
        inst.bookProgressStatus.textContent = `[第 ${curPage}/${totalPages} 页] ${phase}`;
    }
    const pct = totalPages > 0 ? Math.round((curPage / totalPages) * 100) : 0;
    if (inst.bookProgressBarFill) {
        inst.bookProgressBarFill.style.width = `${pct}%`;
    }
    if (inst.bookProgressPercent) {
        inst.bookProgressPercent.textContent = `${pct}% (${curPage}/${totalPages})`;
    }
};

window.onNativeBatchDone = (successCount, failCount, outDir) => {
    const inst = window.app || app;
    if (inst) {
        inst.lastOutputDir = outDir || '';
        inst.tasks.forEach(t => {
            if (t.status !== 'success' && t.status !== 'error') {
                t.status = (failCount === 0) ? 'success' : 'error';
            }
        });
        
        if (inst.bookProgressBarFill) {
            inst.bookProgressBarFill.style.width = '100%';
        }
        if (inst.bookProgressPercent) {
            inst.bookProgressPercent.textContent = `100% (${successCount}/${inst.tasks.length})`;
        }
        if (inst.bookProgressStatus) {
            const isZh = inst.currentLang === 'zh';
            inst.bookProgressStatus.textContent = isZh ? 
                `✔ 转换完成！(成功 ${successCount} 项${failCount > 0 ? ', 失败 ' + failCount + ' 项' : ''})` :
                `✔ Conversion completed! (${successCount} succeeded${failCount > 0 ? ', ' + failCount + ' failed' : ''})`;
        }
        
        inst.isProcessing = false;
        if (inst.btnCancelConvert) inst.btnCancelConvert.style.display = 'none';
        inst.btnStartBookConvert.style.display = 'none';
        inst.btnDownloadBookResult.style.display = 'inline-flex';
        inst.btnConvertAnotherBook.style.display = 'inline-flex';

        setTimeout(() => {
            const isZh = inst.currentLang === 'zh';
            const msg = isZh ? 
                `${inst.currentBookName} 转换完成！已保存` : 
                `${inst.currentBookName} converted successfully! Saved`;
            inst.showToast(msg);
        }, 250);
    }
};
