/**
 * JP2 to JPG/PNG Desktop Converter
 * DjVu.js Inspired Minimalist Architecture & Dedicated Book Conversion View
 */

class JP2ConverterApp {
    constructor() {
        this.tasks = [];
        this.isProcessing = false;
        this.selectedFormat = 'jpg';
        this.quality = 90;
        this.threads = 4;
        this.currentLang = 'zh';
        this.currentBookName = '未命名书籍';
        this.currentFolderPath = '';
        this.lastOutputDir = '';

        this.i18n = {
            zh: {
                title: "EzConv v.1.0.0",
                subtitle: "(基于 OpenJPEG 官方解码内核)",
                linkOptions: "- 查看所有选项",
                linkAbout: "- 了解更多",
                placeholder: "在此处粘贴或输入 jp2 文件的地址",
                btnOpen: "打开地址",
                dropzone: "将文件拖拽至此处或者单击手动选择",
                dropzoneSub: "支持单个/多个 .jp2 文件或文件夹",
                modalTitle: "选项",
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
                title: "EzConv v.1.0.0",
                subtitle: "(Based on OpenJPEG Engine)",
                linkOptions: "- View Options",
                linkAbout: "- Learn More",
                placeholder: "Paste or enter jp2 file address here",
                btnOpen: "Open Path",
                dropzone: "Drag and drop files here, or click to choose manually",
                dropzoneSub: "Supports single/multiple .jp2 files or folders",
                modalTitle: "Options",
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
        this.bindEvents();
        this.initTheme();
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
        const isNative = window.chrome && window.chrome.webview;

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
            this.qualityRow.style.display = (e.target.value === 'jpg') ? 'flex' : 'none';
        });

        // Dropzone click -> Native Files Picker (Supports single/multiple .jp2 files)
        this.dropzone.addEventListener('click', () => {
            if (isNative) {
                window.chrome.webview.postMessage(JSON.stringify({ type: 'select_files' }));
            } else {
                this.fileInput.click();
            }
        });

        this.fileInput.addEventListener('change', (e) => this.handleFileSelect(e.target.files));
        this.folderInput.addEventListener('change', (e) => this.handleFileSelect(e.target.files));

        // Drag & Drop
        ['dragenter', 'dragover'].forEach(name => {
            this.dropzone.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.dropzone.classList.add('drag-over');
            });
        });

        ['dragleave', 'drop'].forEach(name => {
            this.dropzone.addEventListener(name, (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.dropzone.classList.remove('drag-over');
            });
        });

        this.dropzone.addEventListener('drop', (e) => this.handleDrop(e));

        // Address bar open button
        this.openPathBtn.addEventListener('click', () => {
            const rawPath = this.pathInput.value.trim();
            if (!rawPath) {
                if (isNative) {
                    window.chrome.webview.postMessage(JSON.stringify({ type: 'select_folder' }));
                } else {
                    this.fileInput.click();
                }
                return;
            }
            if (isNative) {
                window.chrome.webview.postMessage(JSON.stringify({ 
                    type: 'select_path_input', 
                    path: rawPath 
                }));
            } else {
                this.showToast('请直接点击上方选择文件或输入路径');
            }
        });

        this.pathInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') this.openPathBtn.click();
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
                if (isNative) {
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
        const savedTheme = localStorage.getItem('app-theme') || 
            (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
        this.setTheme(savedTheme);
    }

    setTheme(theme) {
        const t = (theme === 'dark') ? 'dark' : 'light';
        document.documentElement.setAttribute('data-theme', t);
        localStorage.setItem('app-theme', t);
        if (this.themeSunBtn) this.themeSunBtn.classList.toggle('active', t === 'light');
        if (this.themeMoonBtn) this.themeMoonBtn.classList.toggle('active', t === 'dark');
    }

    toggleTheme() {
        const currentTheme = document.documentElement.getAttribute('data-theme') || 'light';
        const nextTheme = currentTheme === 'dark' ? 'light' : 'dark';
        this.setTheme(nextTheme);
    }

    isJP2File(filename) {
        return /\.(jp2|j2k|jpf|jpc)$/i.test(filename);
    }

    async handleDrop(e) {
        const items = e.dataTransfer.items;
        if (items) {
            const files = [];
            for (let i = 0; i < items.length; i++) {
                const entry = items[i].webkitGetAsEntry ? items[i].webkitGetAsEntry() : null;
                if (entry) {
                    await this.traverseFileTree(entry, files);
                } else {
                    const file = items[i].getAsFile();
                    if (file && this.isJP2File(file.name)) files.push(file);
                }
            }
            this.addFiles(files);
        } else {
            this.handleFileSelect(e.dataTransfer.files);
        }
    }

    async traverseFileTree(item, fileList) {
        if (item.isFile) {
            return new Promise((resolve) => {
                item.file((file) => {
                    if (this.isJP2File(file.name)) fileList.push(file);
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
        const validFiles = Array.from(fileList).filter(f => this.isJP2File(f.name));
        if (validFiles.length === 0) {
            this.showToast('未检测到 .jp2 文件');
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
                    path: filePath
                },
                status: 'queued',
                error: null
            });
        });

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
        
        if (langIsZh) {
            this.bookMetaText.textContent = `共 ${this.tasks.length} 页 · 目标格式: ${fmt}${qualityStr} · 并发线程: ${this.threads}`;
        } else {
            this.bookMetaText.textContent = `Total: ${this.tasks.length} pages · Format: ${fmt}${qualityStr} · Threads: ${this.threads}`;
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
                threads: this.threads.toString()
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
document.addEventListener('DOMContentLoaded', () => {
    app = new JP2ConverterApp();
});

// Global Native Callbacks called from C++ Core
window.onNativeFilesSelected = (files) => {
    if (app && files) {
        app.addFiles(files.map(f => ({ name: f.name, size: f.size, path: f.path })));
    }
};

window.onNativeFolderSelected = (folder, files) => {
    if (app && files) {
        app.pathInput.value = folder;
        app.addFiles(files.map(f => ({ name: f.name, size: f.size, path: f.path })), folder);
    }
};

window.onNativeProgress = function() {
    if (!app) return;
    
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
        task = app.tasks.find(t => t.id === taskId);
    }
    if (!task && filename) {
        task = app.tasks.find(t => t.file.name === filename || (t.file.path && t.file.path.endsWith(filename)));
    }
    if (!task && curr > 0 && curr <= app.tasks.length) {
        task = app.tasks[curr - 1];
    }

    if (task) {
        task.status = success ? 'success' : 'error';
        task.error = errorMsg;
    }

    app.updateProgress(curr, total, filename);
};

window.onNativeBatchDone = (successCount, failCount, outDir) => {
    if (app) {
        app.lastOutputDir = outDir || '';
        app.tasks.forEach(t => {
            if (t.status !== 'success' && t.status !== 'error') {
                t.status = (failCount === 0) ? 'success' : 'error';
            }
        });
        
        if (app.bookProgressBarFill) {
            app.bookProgressBarFill.style.width = '100%';
        }
        if (app.bookProgressPercent) {
            app.bookProgressPercent.textContent = `100% (${successCount}/${app.tasks.length})`;
        }
        if (app.bookProgressStatus) {
            const isZh = app.currentLang === 'zh';
            app.bookProgressStatus.textContent = isZh ? 
                `✔ 转换完成！(成功 ${successCount} 项${failCount > 0 ? ', 失败 ' + failCount + ' 项' : ''})` :
                `✔ Conversion completed! (${successCount} succeeded${failCount > 0 ? ', ' + failCount + ' failed' : ''})`;
        }
        
        app.isProcessing = false;
        if (app.btnCancelConvert) app.btnCancelConvert.style.display = 'none';
        app.btnStartBookConvert.style.display = 'none';
        app.btnDownloadBookResult.style.display = 'inline-flex';
        app.btnConvertAnotherBook.style.display = 'inline-flex';

        setTimeout(() => {
            const isZh = app.currentLang === 'zh';
            const msg = isZh ? 
                `${app.currentBookName} 转换完成！已保存` : 
                `${app.currentBookName} converted successfully! Saved`;
            app.showToast(msg);
        }, 250);
    }
};
