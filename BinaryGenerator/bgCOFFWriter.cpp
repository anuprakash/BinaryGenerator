#include "pch.h"
#include "bgFoundation.h"
#include "bgContext.h"
#include "bgString.h"
#include "bgSymbol.h"
#include "bgRelocation.h"
#include "bgSection.h"
#include "bgCOFF.h"
#include "bgCOFFWriter.h"


// structure of COFF file

//--------------------------//
// IMAGE_FILE_HEADER        //
//--------------------------//
// IMAGE_SECTION_HEADER     //
//  * num sections          //
//--------------------------//
//                          //
//                          //
//                          //
// section data             //
//  * num sections          //
//                          //
//                          //
//--------------------------//
// IMAGE_SYMBOL             //
//  * num symbols           //
//--------------------------//
// string table             //
//--------------------------//

namespace bg {

template<class T>
COFFWriter<T>::COFFWriter()
    : m_ctx()
    , m_os()
{
}

template<class Traits> struct COFFImpl;

template<>
struct COFFImpl<Traits_x86>
{
    static const WORD Machine = IMAGE_FILE_MACHINE_I386;
};

template<>
struct COFFImpl<Traits_x86_64>
{
    static const WORD Machine = IMAGE_FILE_MACHINE_AMD64;
};

template<class T>
bool COFFWriter<T>::write(Context& ctx, std::ostream& os)
{
    typedef COFFImpl<T> ImplT;

    m_ctx = &ctx;
    m_os = &os;

    auto& sections = m_ctx->getSections();
    auto& symbols = m_ctx->getSymbolTable().getSymbols();
    auto& strings = m_ctx->getStringTable().getData();

    IMAGE_FILE_HEADER file_header;
    std::vector<IMAGE_SECTION_HEADER> section_headers;
    std::vector<std::vector<IMAGE_RELOCATION>> image_relocations;
    std::vector<IMAGE_SYMBOL> image_symbols;

    DWORD pos_sections = DWORD(
        IMAGE_SIZEOF_FILE_HEADER +
        IMAGE_SIZEOF_SECTION_HEADER * sections.size()
    );
    DWORD pos_symbols = pos_sections;
    for (auto& s : sections) {
        pos_symbols += (DWORD)s->getData().size();
        pos_symbols += (DWORD)(sizeof(IMAGE_RELOCATION) * s->getRelocations().size());
    }

    file_header.Machine = ImplT::Machine;
    file_header.NumberOfSections = (WORD)sections.size();
    file_header.TimeDateStamp = 0;
    file_header.PointerToSymbolTable = pos_symbols;
    file_header.NumberOfSymbols = (DWORD)symbols.size();
    file_header.SizeOfOptionalHeader = 0;
    file_header.Characteristics = 0;

    section_headers.resize(sections.size());
    image_relocations.resize(sections.size());
    image_symbols.resize(symbols.size());

    // build symbol info
    for (size_t si = 0; si < symbols.size(); ++si) {
        auto& sym = symbols[si];
        auto& isym = image_symbols[si];

        isym.N.Name.Short = 0;
        isym.N.Name.Long = sym.name.rva;
        isym.Value = sym.rva;
        isym.SectionNumber = sym.section ? sym.section->getIndex() + 1 : 0;
        isym.Type = IMAGE_SYM_TYPE_NULL;
        isym.StorageClass = IMAGE_SYM_CLASS_NULL;
        if ((sym.flags & SymbolFlags_Static)) {
            isym.StorageClass = IMAGE_SYM_CLASS_STATIC;
        }
        if ((sym.flags & SymbolFlags_External)) {
            isym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
        }
        isym.NumberOfAuxSymbols = 0;
    }

    // build section info
    for (size_t si = 0; si < sections.size(); ++si) {
        auto& section = sections[si];
        auto& data = section->getData();
        auto& rels = section->getRelocations();

        auto& sh = section_headers[si];
        auto& irels = image_relocations[si];

        memcpy(sh.Name, section->getName(), 8);
        sh.VirtualAddress = 0;
        sh.SizeOfRawData = (DWORD)data.size();
        sh.PointerToRawData = pos_sections;
        sh.PointerToRelocations = rels.empty() ? 0 : pos_sections + (DWORD)data.size();
        sh.PointerToLinenumbers = 0;
        sh.NumberOfRelocations = (WORD)rels.size();
        sh.NumberOfLinenumbers = 0;
        sh.Characteristics = translateSectionFlags(section->getFlags());

        irels.resize(rels.size());
        for (size_t ri = 0; ri < rels.size(); ++ri) {
            auto& rel = rels[ri];
            auto& irel = irels[ri];
        }

        pos_sections += sh.SizeOfRawData;
        pos_sections += sizeof(IMAGE_RELOCATION) * sh.NumberOfRelocations;
    }


    // write actual data

    // file header
    m_os->write((char*)&file_header, IMAGE_SIZEOF_FILE_HEADER);

    // section headers
    for (auto& sh : section_headers) {
        m_os->write((char*)&sh, IMAGE_SIZEOF_SECTION_HEADER);
    }

    // section contents
    for (auto& section : sections) {
        auto& data = section->getData();
        auto& rels = section->getRelocations();
        if (!data.empty()) {
            m_os->write(data.c_str(), data.size());
        }
        if (!rels.empty()) {
            m_os->write((char*)&rels[0], sizeof(IMAGE_RELOCATION) * rels.size());
        }
    }

    // symbol table
    for (auto& s : image_symbols) {
        m_os->write((char*)&s, IMAGE_SIZEOF_SYMBOL);
    }

    // string table
    {
        DWORD len = (DWORD)strings.size() + sizeof(DWORD);
        m_os->write((char*)&len, sizeof(len));
        m_os->write(strings.c_str(), strings.size());
    }

    return true;
}

template<class T>
uint32_t COFFWriter<T>::translateSectionFlags(uint32_t flags)
{
    uint32_t r = 0;
    if ((flags & SectionFlag_Code)) {
        r |= IMAGE_SCN_CNT_CODE;
        r |= IMAGE_SCN_ALIGN_16BYTES;
    }
    if ((flags & SectionFlag_IData)) {
        r |= IMAGE_SCN_CNT_INITIALIZED_DATA;
        r |= IMAGE_SCN_ALIGN_16BYTES;
    }
    if ((flags & SectionFlag_Udata)) {
        r |= IMAGE_SCN_CNT_UNINITIALIZED_DATA;
        r |= IMAGE_SCN_ALIGN_16BYTES;
    }
    if ((flags & SectionFlag_Info)) {
        r |= IMAGE_SCN_LNK_INFO;
        r |= IMAGE_SCN_ALIGN_1BYTES;
    }
    if ((flags & SectionFlag_Read)) {
        r |= IMAGE_SCN_MEM_READ;
    }
    if ((flags & SectionFlag_Write)) {
        r |= IMAGE_SCN_MEM_WRITE;
    }
    if ((flags & SectionFlag_Execute)) {
        r |= IMAGE_SCN_MEM_EXECUTE;
    }
    if ((flags & SectionFlag_Shared)) {
        r |= IMAGE_SCN_MEM_SHARED;
    }
    if ((flags & SectionFlag_Remove)) {
        r |= IMAGE_SCN_LNK_REMOVE;
    }
    return r;
}

template class COFFWriter<Traits_x86>;
template class COFFWriter<Traits_x86_64>;

} // namespace bg
