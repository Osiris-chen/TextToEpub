//
// Created by hasee on 2021/11/30.
//

#include "Book.h"

#include <iomanip>
#include <boost/filesystem.hpp>
#include <codecvt>
#include <utility>

using namespace std;

void saveJson(const json &j, const string &name, const string& root) {
    ofstream o(root + name);
    o << setw(2) << j << endl;
}

void Book::saveJson(const json &j, const string &name, const string &root) {
    ofstream o(root + name);
    o << setw(2) << j << endl;
}

json getJson(const string &name, const string& root) {
    json j;
    ifstream i(root + name);
    i >> j;
    return j;
}

json Book::getJson(const string &name, const string &root) {
    json j;
    ifstream i(root + name);
    i >> j;
    return j;
}

void Book::Create(const string &dir) {
    ifstream i("../template/template.json");
    ofstream o(dir + "/newBook.json");
    o << i.rdbuf();
}

Book::Book() = default;

std::ostream &operator<<(std::ostream &out, Book &book) {
    out << setw(4) << json(book) << endl;
    return out;
}

//code transformer  utf-8 -> utf-16
std::wstring WS(const string &s)  {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> cvt_utf;
    return cvt_utf.from_bytes(s);
}

void mkDir(const string &path, const function<void(const string &path)> &visit) {
    boost::filesystem::create_directory(WS(path));
    visit(path);
}

bool find(const string& target, const string& text) {
    regex expression("^\\s*" + target + "\\s*$");
    return regex_match(text, expression);
}

string Book::eBookName() const {
    return "[" + contributor.author + "]." + metadata.title + " " + metadata.subtitle + "." + metadata.volume;
}

string Book::fullTitle() const {
    return metadata.title + " " + metadata.subtitle + " " + metadata.volume;
}

//copy all files in the src_path
void copyFiles(const string &src_path, const string &dist_path) {
    boost::filesystem::path path(src_path);
    boost::filesystem::directory_iterator dir_end;
    for (boost::filesystem::directory_iterator dir_it(path); dir_it != dir_end; ++dir_it) {
        boost::filesystem::copy(dir_it->path(), WS(dist_path + dir_it->path().filename().string()) );
    }
}

void Book::BuildInit(const string &path) {
    CreateResourceDir(path);
}

void Book::CreateResourceDir(const string &path) {
    ResourceRoot = path + "resources/";
    boost::filesystem::create_directory(ResourceRoot);
    boost::filesystem::create_directory(ImagesRoot());
    boost::filesystem::create_directory(TextRoot());
    boost::filesystem::create_directory(DataRoot());
    saveJson(*this, "new book.json", DataRoot());
}

string Book::dir_path() {
    return book_dir + dir_name + "/";
}

void Book::CreateBuildDir(const string &path) {

    book_dir = path;
    pugi::xml_document doc;

    if (boost::filesystem::exists(dir_path())) {
        boost::filesystem::remove_all(dir_path());
    }

    mkDir(dir_path(), [this](const string &path) {

        ofstream os(path + "mimetype");
        os << "application/epub+zip";
        os.close();

        mkDir(path + "META-INF/", [](const string &path) {
            pugi::xml_document doc;
            doc.load_file((TemplateRoot + "container.xml").data());
            doc.save_file((path + "container.xml").data());
        });

        mkDir(path + "EPUB/", [this](const string &path) {

            pugi::xml_document doc;
            doc.load_file((TemplateRoot + "package.opf").data());
            doc.save_file((path + "package.opf").data());

            mkDir(path + "Styles/",[](const string &path) {
                boost::filesystem::copy_file(TemplateRoot + "Styles/style.css", path + "style.css");
            });
            mkDir(path + "Images/", [this](const string &path) {
                copyFiles(ImagesRoot(), path);
            });
        });
    });
}

void Book::PackBook() {
    if (!book_dir.empty()) {
        wstring new_dir = WS(book_dir + eBookName());
        if (boost::filesystem::exists(new_dir)) {
            boost::filesystem::remove_all(new_dir);
        }
        boost::filesystem::rename(book_dir + dir_name, new_dir);
    }
}

string Book::wrap(string wrapped, bool blank) const {
    boost::replace_last(wrapped, ".", "\\.");
    wrapped = "^(" + wrapped + ")$";
    if (blank)
        wrapped.insert(1, "[" + whitespace + "]*");
    return wrapped;
}

struct StatusProcess {
    function<void()> begin, process;
};

void Book::extract(const string &inputTextPath, const string &outPutDir) {
    ifstream text(inputTextPath);
    if (!text.is_open()) {
        cout << "no file found in " + inputTextPath << endl;
        return;
    }

    string line;
    whitespace = boost::join(metadata.whitespace, "") + "\\s";

    vector<string> list;
    if (!contents.preface.empty()) list.push_back(contents.preface);
    list.insert(list.end(), illustrations.color.begin(), illustrations.color.end());
    list.insert(list.end(), contents.chapters.begin(), contents.chapters.end());

    context::Section *section;
    context::Chapter *chapter;

    map<string, regex*> expression;
    map<string, StatusProcess*> statusProcess;

    queue<string> status_queue;

    expression["separator"] = new regex(wrap(boost::join(metadata.separators, "|"), true));
    expression["space"] = new regex("^[" + whitespace + "]*");

    auto setStatus = [&status_queue] (string &status) {
        status = status_queue.front();
        status_queue.pop();
    };

    context::Annotation note = metadata.note;

    map<string, string> key;
    map<string, function<void()>> update;

    auto note_record = [&] (context::Section *section, string &line) {
        unsigned long long pos = string::npos;

        int index = (int)section->paragraphs.size();

        if (!notes.empty() && line.find(notes.front().mark(context::Annotation::title_only)) != string::npos) {
            int i = notes.front().number;
            section->note_pairs[i].line = index;
            notes.pop();
            return;
        }

        while (pos = line.find(note.mark(), pos + 1), pos != string::npos) {
            notes.push(note);
            section->note_pairs[note.number] = {note, index};
            note.number++;
        }

    };

    auto appendParagraph = [&expression, &line, &note_record] (context::Section *section) {

        if (!line.empty()) {
            section->setInterval();

            if (regex_match(line, *expression["separator"])) {
                int index = section->paragraphs.size();
                section->separators.emplace_back(index);
                line = regex_replace(line, *expression["space"], "");
            }

            note_record(section, line);
        }

        section->paragraphs.emplace_back(line);
    };

    status_queue.push("start");
    statusProcess["start"] = new StatusProcess { [] {}, [] {} };
    for (const auto &title: list) {
        status_queue.push(title);
        expression[title] = new regex(wrap(title));
        statusProcess[title] = new StatusProcess {
            [&] {
                chapter = new context::Chapter(lang, title);
                chapter->nextSection = metadata.sectionNumberBegin;
                expression["section"] = new regex(wrap(to_string(chapter->nextSection), true));
                chapters.emplace_back(chapter);
                std::find(illustrations.color.begin(), illustrations.color.end(), title) == illustrations.color.end()
                ? chapter->type = context::single_chapter : chapter->type = context::illustration;
            },
            [&] {
                if (regex_match(line, *expression["section"])) {
                    section = new context::Section();
                    section->title = line.back();
                    chapter->sections.emplace_back(section);
                    expression["section"] = new regex(wrap(to_string(++chapter->nextSection), true));
                    chapter->type = context::within_sections;
                } else switch (chapter->type) {
                    case context::within_sections:
                        appendParagraph(section);
                        break;
                    case context::single_chapter:
                    case context::illustration:
                        appendParagraph(chapter);
                }
            }
        };
    }

    string status;
    setStatus(status);
    while (!text.eof()) {

        getline(text, line);

        if (!status_queue.empty() && regex_match(line, *expression[status_queue.front()])) {
            setStatus(status);
            statusProcess[status]->begin();
        } else {
            statusProcess[status]->process();
        }
    }

    string name;

    if (!metadata.cover.empty()) {
        name = "cover.xhtml";
        pugi::xml_document cover;
        cover.load_file((TemplateRoot + name).data());
        auto html = cover.child("html");
        html.attribute("xml:lang") = lang.data();
        html.attribute("lang") = lang.data();
        auto img = html.child("body").child("figure").child("img");
        img.attribute("src") = ("Images/" + metadata.cover).data();
        img.attribute("alt") = metadata.cover.data();
        cover.save_file((outPutDir + name).data());
    }

    auto image = illustrations.gray.begin();
    pugi::xml_document doc;
    for (int i = 0; i < chapters.size(); ++i) {
        name = "chapter" + to_string(i) + ".xhtml";
        chapters[i]->to_xml(outPutDir + name);

        doc.load_file((outPutDir + name).data());
        auto img_nodes = doc.select_nodes("//img[@src='__illustration__' and @alt='__illustration__']");
        for (auto img: img_nodes) {
            if (image != illustrations.gray.end()) {
                img.node().attribute("src") = ("Images/" + *image).data();
                img.node().attribute("alt") = image->data();
                image++;
            }
        }
        doc.save_file((outPutDir + name).data());
    }

    buildToc(outPutDir);
    buildPackage(outPutDir);
}

void Book::buildPackage(const string &outPutDir) {

    pugi::xml_document doc;
    doc.load_file((TemplateRoot + "package.opf").data());

    auto package = doc.child("package");
    package.attribute("xml:lang") = lang.data();

    auto metadata = package.child("metadata");
    metadata.child("dc:identifier").text() = ("urn:uuid:" + uuid4()).data();
    metadata.child("dc:title").text() = fullTitle().data();
    metadata.child("dc:language").text() = lang.data();
    metadata.child("dc:creator").text() = contributor.author.data();

    auto manifest = package.child("manifest");
    auto spine = package.child("spine");

    auto appendItem = [&] (const string& id, const string& href, const string& media_type)->pugi::xml_node {
        auto item = manifest.append_child("item");
        item.append_attribute("id") = id.data();
        item.append_attribute("href") = href.data();
        item.append_attribute("media-type") = media_type.data();
        return item;
    };

    if (!this->metadata.cover.empty()) {

        appendItem("cover-image", "Images/" + this->metadata.cover, mediaType(this->metadata.cover))
                .append_attribute("properties") = "cover-image";

        appendItem("cover", "cover.xhtml", "application/xhtml+xml");
        spine.append_child("itemref").append_attribute("idref") = "cover";
    }

    for (int i = 0; i < chapters.size(); i++) {
        string title = chapters[i]->title;
        if (chapters[i]->type == context::illustration) {
            auto pos = title.find_last_of('.');
            title.replace(pos, 6, ".xhtml");
        }
        appendItem(title, "chapter" + to_string(i) + ".xhtml", "application/xhtml+xml");
        spine.append_child("itemref").append_attribute("idref") = title.data();
    }

    for (const auto& i : illustrations.color) {
        appendItem(i, "Images/" + i, mediaType(i));
    }

    for (const auto& i : illustrations.gray) {
        appendItem(i, "Images/" + i, mediaType(i));
    }

    doc.save_file((outPutDir + "package.opf").data());
}

void Book::buildToc(const string &outPutDir) {
    string name = "toc.xhtml";

    pugi::xml_document toc;
    toc.load_file((TemplateRoot + name).data());
    auto html = toc.child("html");
    html.attribute("xml:lang") = lang.data();
    html.attribute("lang") = lang.data();
    html.child("head").child("title").text() = fullTitle().data();

    auto body = html.child("body");
    body.child("h1").text() = fullTitle().data();
    auto nav = body.child("nav");
    nav.child("h2").text() = "Contents";

    auto content_list = nav.child("ol");
    auto listAdd = [] (pugi::xml_node &ol, const string &href, const string &title) {
        auto a = ol.append_child("li").append_child("a");
        a.append_attribute("href") = href.data();
        a.text() = title.data();
    };

    for (unsigned long long i = 0; i < chapters.size(); i++) {
        string title;

        if (chapters[i]->type == context::illustration) {
            title = "插图";
            listAdd(content_list, "chapter" + to_string(i) + ".xhtml", title);
            i += illustrations.color.size() - 1;
            continue;
        }

        title = chapters[i]->title;
        listAdd(content_list, "chapter" + to_string(i) + ".xhtml", title);
    }

    toc.save_file((outPutDir + name).data());

}

void Book::addIllustrations(const std::filesystem::path& path, string colorBegin, string grayBegin) {
    std::filesystem::directory_entry entry(path);
    map<string, vector<string> *> illustration;
    map<string, string> beginTag;

    queue<string> status;
    status.push("start");
    status.push("color");
    status.push("gray");

    illustration["color"] = &illustrations.color;
    illustration["gray"] = &illustrations.gray;

    beginTag["color"] = std::move(colorBegin);
    beginTag["gray"] = std::move(grayBegin);

    auto setStatus = [&status] (string &s) {
        s = status.front();
        status.pop();
    };

    string type;
    setStatus(type);

    if (entry.is_directory()) {
        std::filesystem::directory_iterator list(path);

        for (const auto &image: list) {
            if (!status.empty() && image.path().filename() == beginTag[status.front()]) {
                setStatus(type);
            }

            if (type != "start")
                illustration[type]->push_back(image.path().filename().string());
        }
    }
}

string Book::ImagesRoot() {
    return ResourceRoot + "images/";
}

string Book::TextRoot() {
    return ResourceRoot + "text/";
}

string Book::DataRoot() {
    return ResourceRoot + "data/";
}
