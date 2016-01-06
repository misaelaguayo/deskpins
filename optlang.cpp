#include "stdafx.h"
#include "options.h"
#include "util.h"
#include "optlang.h"
#include "resource.h"


const DWORD CHM_MARKER_SIG = 0xefda7a00;  // from chmmark.py


struct Data {
    std::wstring fname, dispName, descr;
    Data(const std::wstring& fname   = L"", 
        const std::wstring& dispName = L"", 
        const std::wstring& descr    = L"")
        : fname(fname), dispName(dispName), descr(descr) {}
};


std::wstring getComboSel(HWND wnd)
{
    std::wstring ret;
    int n = SendMessage(wnd, CB_GETCURSEL, 0, 0);
    if (n != CB_ERR) {
        int i = SendMessage(wnd, CB_GETITEMDATA, n, 0);
        if (i != CB_ERR && i != 0) {
            ret.assign(reinterpret_cast<Data*>(i)->fname);
        }
    }
    return ret;
}


std::wstring getLangFileDescr(const std::wstring& path, const std::wstring& file)
{
    HINSTANCE inst = file.empty() ? app.inst : LoadLibrary((path+file).c_str());
    WCHAR buf[100];
    if (!inst || !LoadString(inst, IDS_LANG, buf, sizeof(buf)))
        *buf = '\0';
    if (!file.empty() && inst)   // release inst if we had to load it
        FreeLibrary(inst);
    return buf;
}


void loadLangFiles(HWND combo, const std::wstring& path, const std::wstring& cur)
{
    std::vector<std::wstring> files = getFiles(path + L"lang*.dll");
    files.push_back(L"");    // special entry

    for (int n = 0; n < int(files.size()); ++n) {
        Data* data = new Data;
        data->fname    = files[n];
        data->dispName = files[n].empty() ? ef::fileSpec(ef::Win::getModulePath(app.inst)) : files[n];
        data->descr    = getLangFileDescr(path, files[n]);
        int i = SendMessage(combo, CB_ADDSTRING, 0, LPARAM(data));
        if (i == CB_ERR)
            delete data;
        else if (strimatch(data->fname.c_str(), cur.c_str()))
            SendMessage(combo, CB_SETCURSEL, i, 0);
    }

}


static bool readFileBack(HANDLE file, void* buf, int bytes)
{
    DWORD read;
    return SetFilePointer(file, -bytes, 0, FILE_CURRENT) != -1
        && ReadFile(file, buf, bytes, &read, 0)
        && int(read) == bytes
        && SetFilePointer(file, -bytes, 0, FILE_CURRENT) != -1;
}


std::wstring getHelpFileDescr(const std::wstring& path, const std::wstring& name)
{
    // Data layout:
    //  {CHM file data...}
    //  WCHAR data[size]  // no NULs
    //  DWORD size
    //  DWORD sig

    std::wstring ret;
    DWORD sig, len;
    ef::Win::AutoFileH file = ef::Win::FileH::create(path + name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file != INVALID_HANDLE_VALUE &&
        file.setPosFromEnd32(0) &&
        readFileBack(file, &sig, sizeof(sig)) && 
        sig == CHM_MARKER_SIG &&
        readFileBack(file, &len, sizeof(len)))
    {
        boost::scoped_array<char> buf(new char[len]);
        if (readFileBack(file, buf.get(), len)) {
            boost::scoped_array<WCHAR> wbuf(new WCHAR[len]);
            if (MultiByteToWideChar(CP_THREAD_ACP, 0, buf.get(), len, wbuf.get(), len))
                ret.assign(wbuf.get(), len);
        }
    }
    return ret;
}


void loadHelpFiles(HWND combo, const std::wstring& path, const std::wstring& cur)
{
    std::vector<std::wstring> files = getFiles(path + L"DeskPins*.chm");

    for (int n = 0; n < int(files.size()); ++n) {
        Data* data = new Data;
        data->fname =    files[n];
        data->dispName = files[n];
        data->descr    = getHelpFileDescr(path, files[n]);
        int i = SendMessage(combo, CB_ADDSTRING, 0, LPARAM(data));
        if (i == CB_ERR)
            delete data;
        else if (strimatch(data->fname.c_str(), cur.c_str()))
            SendMessage(combo, CB_SETCURSEL, i, 0);

    }

}


static bool evInitDialog(HWND wnd, HWND focus, LPARAM param)
{
    // must have a valid data ptr
    if (!param) {
        EndDialog(wnd, IDCANCEL);
        return true;
    }

    // save the data ptr
    PROPSHEETPAGE& psp = *reinterpret_cast<PROPSHEETPAGE*>(param);
    Options& opt = reinterpret_cast<OptionsPropSheetData*>(psp.lParam)->opt;
    SetWindowLong(wnd, DWL_USER, psp.lParam);

    std::wstring exePath = ef::dirSpec(ef::Win::getModulePath(app.inst));
    if (!exePath.empty()) {
#ifdef DEBUG
        loadLangFiles(GetDlgItem(wnd, IDC_UILANG),   exePath + L"..\\localization\\", opt.uiFile);
        loadHelpFiles(GetDlgItem(wnd, IDC_HELPLANG), exePath + L"..\\help\\",         opt.helpFile);
#else
        loadLangFiles(GetDlgItem(wnd, IDC_UILANG),   exePath, opt.uiFile);
        loadHelpFiles(GetDlgItem(wnd, IDC_HELPLANG), exePath, opt.helpFile);
#endif
    }

    return true;
}


static bool validate(HWND wnd)
{
    return true;
}


static void apply(HWND wnd)
{
    Options& opt = reinterpret_cast<OptionsPropSheetData*>(GetWindowLong(wnd, DWL_USER))->opt;

    std::wstring uiFile = getComboSel(GetDlgItem(wnd, IDC_UILANG));
    if (opt.uiFile != uiFile) {
        if (app.loadResMod(uiFile.c_str(), wnd))
            opt.uiFile = uiFile;
        else
            opt.uiFile = L"";
    }

    opt.helpFile = getComboSel(GetDlgItem(wnd, IDC_HELPLANG));
    app.help.init(app.inst, opt.helpFile);
}


BOOL CALLBACK optLangProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
        case WM_INITDIALOG:  return evInitDialog(wnd, HWND(wparam), lparam);
        case WM_NOTIFY: {
            NMHDR nmhdr = *reinterpret_cast<NMHDR*>(lparam);
            switch (nmhdr.code) {
                case PSN_SETACTIVE: {
                    HWND tab = PropSheet_GetTabControl(nmhdr.hwndFrom);
                    app.optionPage = SendMessage(tab, TCM_GETCURSEL, 0, 0);
                    SetWindowLong(wnd, DWL_MSGRESULT, 0);
                    break;
                }
                case PSN_KILLACTIVE: {
                    SetWindowLong(wnd, DWL_MSGRESULT, !validate(wnd));
                    break;
                }
                case PSN_APPLY: {
                    apply(wnd);
                    break;
                }
                case PSN_HELP: {
                    app.help.show(wnd, L"::\\optlang.htm");
                    break;
                }
                default:
                    return false;
            }
            break;
        }
        case WM_COMMAND: {
            WORD code = HIWORD(wparam), id = LOWORD(wparam);
            if (code == CBN_SELCHANGE) {
                if (id == IDC_UILANG || id == IDC_HELPLANG)
                    psChanged(wnd);
            }
            break;
        }
        case WM_HELP: {
            app.help.show(wnd, L"::\\optlang.htm");
            break;
        }
        case WM_DELETEITEM: {
            if (wparam == IDC_UILANG || wparam == IDC_HELPLANG)
                delete (Data*)(LPDELETEITEMSTRUCT(lparam)->itemData);
            break;
        }
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT& mis = *(MEASUREITEMSTRUCT*)lparam;
            mis.itemHeight = ef::Win::FontH(ef::Win::FontH::getStockDefaultGui()).getHeight() + 2;
            break;
        }
        case WM_DRAWITEM: { 
            DRAWITEMSTRUCT& dis = *(DRAWITEMSTRUCT*)lparam;
            RECT& rc = dis.rcItem;
            HDC dc = dis.hDC;      
            if (dis.itemID != -1) {
                bool sel = dis.itemState & ODS_SELECTED;
                FillRect(dc, &rc, GetSysColorBrush(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));

                if (dis.itemData) {
                    Data& data = *reinterpret_cast<Data*>(dis.itemData);

                    UINT     orgAlign  = SetTextAlign(dc, TA_LEFT);
                    COLORREF orgTxtClr = SetTextColor(dc, GetSysColor(sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                    int      orgBkMode = SetBkMode(dc, TRANSPARENT);

                    TextOut(dc, rc.left+2, rc.top+1, data.descr.c_str(), data.descr.length());
                    SetTextAlign(dc, TA_RIGHT);
                    if (!sel) SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
                    TextOut(dc, rc.right-2, rc.top+1, data.dispName.c_str(), data.dispName.length());

                    SetBkMode   (dc, orgBkMode);
                    SetTextColor(dc, orgTxtClr);
                    SetTextAlign(dc, orgAlign);
                }
            }
            break;
        }
        case WM_COMPAREITEM: {
            COMPAREITEMSTRUCT& cis = *(COMPAREITEMSTRUCT*)lparam;
            if (wparam == IDC_UILANG || wparam == IDC_HELPLANG) {
                Data* d1 = (Data*)cis.itemData1;
                Data* d2 = (Data*)cis.itemData2;
                if (d1 && d2) {
                    int order = CompareString(cis.dwLocaleId, NORM_IGNORECASE, 
                        d1->descr.c_str(), d1->descr.length(), 
                        d2->descr.c_str(), d2->descr.length());
                    if (order) return order - 2;    // convert 1/2/3 to -1/0/1
                }
            }
            return 0;
        }
        default:
            return false;
    }

    return true;
}
