#include "Common.hpp"
#include "GetOpt.hpp"
#include "ZCMGen.hpp"
#include "Emitter.hpp"
#include "util/StringUtil.hpp"
#include "util/FileUtil.hpp"

#include <iostream>

static string dotsToUnderscores(const string& s)
{
    return StringUtil::replace(s, '.', '_');
}

static string dotsToDoubleColons(const string& s)
{
    string ret;
    for (auto& c : s) {
        if (c == '.')
            ret += "::";
        else
            ret += string(1, c);
    }
    return ret;
}

static string dotsToSlashes(const string& s)
{
    return StringUtil::replace(s, '.', '/');
}

static string dimSizePrefix(const string& dimSize)
{
    char* eptr;
    int ret = strtol(dimSize.c_str(), &eptr, 0);
    (void) ret;
    if(*eptr == '\0')
        return "";
    else
        return "this->";
}

static string dimSizeAccessor(const string& dimSize)
{
    return dimSizePrefix(dimSize) + dimSize;
}

static bool isDimSizeFixed(const string& dimSize)
{
    char* eptr;
    int ret = strtol(dimSize.c_str(), &eptr, 0);
    (void) ret;
    return (*eptr == '\0');
}

// Some types do not have a 1:1 mapping from zcm types to native C
// storage types.
static string mapTypeName(const string& t)
{
    if (t == "boolean")  return "int8_t";
    if (t == "string")   return "std::string";
    if (t == "byte")     return "uint8_t";
    return dotsToDoubleColons(t);
}

void setupOptionsCpp(GetOpt& gopt)
{
    gopt.addString(0, "cpp-hpath",    ".",      "Location for .hpp files");
    gopt.addString(0, "cpp-include",   "",       "Generated #include lines reference this folder");
}

struct Emit : public Emitter
{
    const ZCMGen& zcm;
    const ZCMStruct& zs;

    Emit(const ZCMGen& zcm, const ZCMStruct& zs, const string& fname):
        Emitter(fname), zcm(zcm), zs(zs) {}

    void emitAutoGeneratedWarning()
    {
        emit(0, "/** THIS IS AN AUTOMATICALLY GENERATED FILE.");
        emit(0, " *  DO NOT MODIFY BY HAND!!");
        emit(0, " *");
        emit(0, " *  Generated by zcm-gen");
        emit(0, " **/");
        emit(0, "");
    }

    void emitComment(int indent, const string& comment)
    {
        if (comment == "")
            return;

        auto lines = StringUtil::split(comment, '\n');
        if (lines.size() == 1) {
            emit(indent, "/// %s", lines[0].c_str());
        } else {
            emit(indent, "/**");
            for (auto& line : lines) {
                if (line.size() > 0) {
                    emit(indent, " * %s", line.c_str());
                } else {
                    emit(indent, " *");
                }
            }
            emit(indent, " */");
        }
    }

    void emitPackageNamespaceStart()
    {
        // output namespace declaration
        auto namespaces = StringUtil::split(zs.structname.fullname, '.');
        for (size_t i = 0; i < namespaces.size()-1; ++i)
            emit(0, "namespace %s {", namespaces[i].c_str());
    }

    void emitPackageNamespaceClose()
    {
        auto namespaces = StringUtil::split(zs.structname.fullname, '.');
        for (size_t i = 0; i < namespaces.size()-1; ++i)
            emit(0, "}\n");
    }

    void emitHeaderStart()
    {
        const char* tn = zs.structname.fullname.c_str();
        const char* sn = zs.structname.shortname.c_str();
        string tnDots = dotsToUnderscores(zs.structname.fullname);
        const char* tn_ = tnDots.c_str();

        emitAutoGeneratedWarning();

        emit(0, "#include <zcm/zcm_coretypes.h>");
        emit(0, "");
        emit(0, "#ifndef __%s_hpp__", tn_);
        emit(0, "#define __%s_hpp__", tn_);
        emit(0, "");

        // do we need to #include <vector> and/or <string>?
        bool emitIncludeVector = false;
        bool emitIncludeString = false;
        for (auto& zm : zs.members) {
            if (!emitIncludeVector &&
                zm.dimensions.size() != 0 && !zm.isConstantSizeArray()) {
                emit(0, "#include <vector>");
                emitIncludeVector = true;
            }
            if (!emitIncludeString &&
                zm.type.fullname == "string") {
                emit(0, "#include <string>");
                emitIncludeString = true;
            }
        }

        // include header files for other ZCM types
        for (auto& zm : zs.members) {
            auto& mtn = zm.type.fullname;
            if (!ZCMGen::isPrimitiveType(mtn) && mtn != tn) {
                emit(0, "#include \"%s%s%s.hpp\"",
                     zcm.gopt->getString("cpp-include").c_str(),
                     zcm.gopt->getString("cpp-include").size()>0 ? "/":"",
                     dotsToSlashes(mtn).c_str());
            }
        }

        emit(0, "\n");
        emitPackageNamespaceStart();

        // define the class
        emitComment(0, zs.comment);
        emit(0, "class %s", sn);
        emit(0, "{");

        // data members
        if(zs.members.size() > 0) {
            emit(1, "public:");
            for (auto& zm : zs.members) {
                auto& mtn = zm.type.fullname;
                emitComment(2, zm.comment);
                string mappedTypename = mapTypeName(mtn);
                int ndim = (int)zm.dimensions.size();
                if (ndim == 0) {
                    emit(2, "%-10s %s;", mappedTypename.c_str(), zm.membername.c_str());
                } else {
                    if (zm.isConstantSizeArray()) {
                        emitStart(2, "%-10s %s", mappedTypename.c_str(), zm.membername.c_str());
                        for (auto& zd : zm.dimensions)
                            emitContinue("[%s]", zd.size.c_str());
                        emitEnd(";");
                    } else {
                        emitStart(2, "");
                        for (int d = 0; d < ndim; ++d)
                            emitContinue("std::vector< ");
                        emitContinue("%s", mappedTypename.c_str());
                        for (int d = 0; d < ndim; ++d)
                            emitContinue(" >");
                        emitEnd(" %s;", zm.membername.c_str());
                    }
                }
                emit(0, "");
            }
        }

        // constants
        if (zs.constants.size() > 0) {
            emit(1, "public:");
            emit(2, "#if __cplusplus > 199711L /* if c++11 */");
            emit(2, "static constexpr %-8s IS_LITTLE_ENDIAN = %s;", "int8_t",
                    zcm.gopt->getBool("little-endian-encoding") ? "1" : "0");
            for (auto& zc : zs.constants) {
                assert(ZCMGen::isLegalConstType(zc.type));
                emitComment(2, zc.comment);
                string mt = mapTypeName(zc.type);
                const char* suffix = zc.type == "int64_t" ? "LL" : "";
                emit(2, "static constexpr %-8s %s = %s%s;", mt.c_str(),
                        zc.membername.c_str(), zc.valstr.c_str(), suffix);
            }
            emit(2, "#else");
            emit(2, "static const     %-8s IS_LITTLE_ENDIAN = %s;", "int8_t",
                    zcm.gopt->getBool("little-endian-encoding") ? "1" : "0");
            for (auto& zc : zs.constants) {
                assert(ZCMGen::isLegalConstType(zc.type));
                string mt = mapTypeName(zc.type);
                const char* suffix = zc.type == "int64_t" ? "LL" : "";
                emit(2, "static const     %-8s %s = %s%s;", mt.c_str(),
                        zc.membername.c_str(), zc.valstr.c_str(), suffix);
            }
            emit(2, "#endif");
            emit(0, "");
        }

        emit(1, "public:");
        emit(2, "/**");
        emit(2, " * Destructs a message properly if anything inherits from it");
        emit(2, "*/");
        emit(2, "virtual ~%s() {}", zs.structname.shortname.c_str());
        emit(0, "");
        emit(2, "/**");
        emit(2, " * Encode a message into binary form.");
        emit(2, " *");
        emit(2, " * @param buf The output buffer.");
        emit(2, " * @param offset Encoding starts at thie byte offset into @p buf.");
        emit(2, " * @param maxlen Maximum number of bytes to write.  This should generally be");
        emit(2, " *  equal to getEncodedSize().");
        emit(2, " * @return The number of bytes encoded, or <0 on error.");
        emit(2, " */");
        emit(2, "inline int encode(void* buf, uint32_t offset, uint32_t maxlen) const;");
        emit(0, "");
        emit(2, "/**");
        emit(2, " * Check how many bytes are required to encode this message.");
        emit(2, " */");
        emit(2, "inline uint32_t getEncodedSize() const;");
        emit(0, "");
        emit(2, "/**");
        emit(2, " * Decode a message from binary form into this instance.");
        emit(2, " *");
        emit(2, " * @param buf The buffer containing the encoded message.");
        emit(2, " * @param offset The byte offset into @p buf where the encoded message starts.");
        emit(2, " * @param maxlen The maximum number of bytes to reqad while decoding.");
        emit(2, " * @return The number of bytes decoded, or <0 if an error occured.");
        emit(2, " */");
        emit(2, "inline int decode(const void* buf, uint32_t offset, uint32_t maxlen);");
        emit(0, "");
        emit(2, "/**");
        emit(2, " * Retrieve the 64-bit fingerprint identifying the structure of the message.");
        emit(2, " * Note that the fingerprint is the same for all instances of the same");
        emit(2, " * message type, and is a fingerprint on the message type definition, not on");
        emit(2, " * the message contents.");
        emit(2, " */");
        emit(2, "inline static int64_t getHash();");
        emit(0, "");
        emit(2, "/**");
        emit(2, " * Returns \"%s\"", zs.structname.shortname.c_str());
        emit(2, " */");
        emit(2, "inline static const char* getTypeName();");

        emit(0, "");
        emit(2, "// ZCM support functions. Users should not call these");
        emit(2, "inline int      _encodeNoHash(void* buf, uint32_t offset, uint32_t maxlen) const;");
        emit(2, "inline uint32_t _getEncodedSizeNoHash() const;");
        emit(2, "inline int      _decodeNoHash(const void* buf, uint32_t offset, uint32_t maxlen);");
        emit(2, "inline static uint64_t _computeHash(const __zcm_hash_ptr* p);");
        emit(0, "};");
        emit(0, "");
    }

    void emitHeaderEnd()
    {
        emitPackageNamespaceClose();
        emit(0, "#endif");
    }

    void emitEncode()
    {
        const char* sn = zs.structname.shortname.c_str();
        emit(0, "int %s::encode(void* buf, uint32_t offset, uint32_t maxlen) const", sn);
        emit(0, "{");
        emit(1,     "uint32_t pos = 0;");
        emit(1,     "int thislen;");
        emit(1,     "int64_t hash = (int64_t)getHash();");
        emit(0, "");
        emit(1,     "thislen = __int64_t_encode_array(buf, offset + pos, maxlen - pos, &hash, 1);");
        emit(1,     "if(thislen < 0) return thislen; else pos += thislen;");
        emit(0, "");
        emit(1,     "thislen = this->_encodeNoHash(buf, offset + pos, maxlen - pos);");
        emit(1,     "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0, "");
        emit(1,     "return pos;");
        emit(0, "}");
        emit(0, "");
    }

    void emitEncodedSize()
    {
        const char* sn = zs.structname.shortname.c_str();
        emit(0,"uint32_t %s::getEncodedSize() const", sn);
        emit(0,"{");
        emit(1, "return 8 + _getEncodedSizeNoHash();");
        emit(0,"}");
        emit(0,"");
    }

    void emitDecode()
    {
        const char* sn = zs.structname.shortname.c_str();
        emit(0, "int %s::decode(const void* buf, uint32_t offset, uint32_t maxlen)", sn);
        emit(0, "{");
        emit(1,     "uint32_t pos = 0;");
        emit(1,     "int thislen;");
        emit(0, "");
        emit(1,     "int64_t msg_hash;");
        emit(1,     "thislen = __int64_t_decode_array(buf, offset + pos, maxlen - pos, &msg_hash, 1);");
        emit(1,     "if (thislen < 0) return thislen; else pos += thislen;");
        emit(1,     "if (msg_hash != getHash()) return -1;");
        emit(0, "");
        emit(1,     "thislen = this->_decodeNoHash(buf, offset + pos, maxlen - pos);");
        emit(1,     "if (thislen < 0) return thislen; else pos += thislen;");
        emit(0, "");
        emit(1,  "return pos;");
        emit(0, "}");
        emit(0, "");
    }

    void emitGetHash()
    {
        const char* sn = zs.structname.shortname.c_str();
        emit(0, "int64_t %s::getHash()", sn);
        emit(0, "{");
        emit(1,     "static int64_t hash = _computeHash(NULL);");
        emit(1,     "return hash;");
        emit(0, "}");
        emit(0, "");
    }

    void emitGetTypeName()
    {
        auto* sn = zs.structname.shortname.c_str();
        emit(0, "const char* %s::getTypeName()", sn);
        emit(0, "{");
        emit(1,     "return \"%s\";", sn);
        emit(0, "}");
        emit(0, "");
    }

    void emitComputeHash()
    {
        const char* sn = zs.structname.shortname.c_str();

        int lastComplexMember = -1;
        for (int m = 0; m < (int)zs.members.size(); ++m) {
            auto& zm = zs.members[m];
            if (!ZCMGen::isPrimitiveType(zm.type.fullname))
                lastComplexMember = m;
        }

        if (lastComplexMember >= 0) {
            emit(0, "uint64_t %s::_computeHash(const __zcm_hash_ptr* p)", sn);
            emit(0, "{");
            emit(1,     "const __zcm_hash_ptr* fp;");
            emit(1,     "for(fp = p; fp != NULL; fp = fp->parent)");
            emit(2,         "if(fp->v == %s::getHash)", sn);
            emit(3,              "return 0;");
            if (zs.members.size() > 0) {
                emit(1, "const __zcm_hash_ptr cp = { p, (void*)%s::getHash };", sn);
            }
            emit(0, "");
            emit(1,     "uint64_t hash = (uint64_t)0x%016" PRIx64 "LL +", zs.hash);

            for (int m = 0; m < (int)zs.members.size(); ++m) {
                auto& zm = zs.members[m];
                auto& mtn = zm.type.fullname;
                if (!ZCMGen::isPrimitiveType(mtn)) {
                    emit(2, " %s::_computeHash(&cp)%s",
                         dotsToDoubleColons(mtn).c_str(),
                         (m == lastComplexMember) ? ";" : " +");
                }
            }
            emit(0, "");
        } else {
            emit(0, "uint64_t %s::_computeHash(const __zcm_hash_ptr*)", sn);
            emit(0, "{");
            emit(1,     "uint64_t hash = (uint64_t)0x%016" PRIx64 "LL;", zs.hash);
        }

        emit(1, "return (hash<<1) + ((hash>>63)&1);");
        emit(0, "}");
        emit(0, "");
    }

    void _encodeRecursive(const ZCMMember& zm, int depth, int extraIndent)
    {
        auto& mtn = zm.type.fullname;
        int indent = extraIndent + 1 + depth;
        int ndims = (int)zm.dimensions.size();
        // primitive array
        if (depth+1 == ndims &&
            ZCMGen::isPrimitiveType(mtn) && mtn != "string") {

            auto& dim = zm.dimensions[depth];
            emitStart(indent, "thislen = __%s_encode_%sarray(buf, offset + pos, maxlen - pos, &this->%s",
                      mtn.c_str(),
                      zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                      zm.membername.c_str());
            for(int i = 0; i < depth; ++i)
                emitContinue("[a%d]", i);
            emitEnd("[0], %s%s);", dimSizePrefix(dim.size).c_str(), dim.size.c_str());

            emit(indent, "if(thislen < 0) return thislen; else pos += thislen;");
            return;
        }
        if(depth == ndims) {
            if(mtn == "string") {
                emitStart(indent, "char* __cstr = (char*) this->%s", zm.membername.c_str());
                for(int i = 0; i < depth; ++i)
                    emitContinue("[a%d]", i);
                emitEnd(".c_str();");
                emit(indent, "thislen = __string_encode_%sarray(buf, offset + pos, maxlen - pos, &__cstr, 1);",
                             zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "");
            } else {
                emitStart(indent, "thislen = this->%s", zm.membername.c_str());
                for(int i = 0; i < depth; ++i)
                    emitContinue("[a%d]", i);
                emitEnd("._encodeNoHash(buf, offset + pos, maxlen - pos);");
            }
            emit(indent, "if(thislen < 0) return thislen; else pos += thislen;");
            return;
        }

        auto& dim = zm.dimensions[depth];
        emit(indent, "for (int a%d = 0; a%d < %s%s; ++a%d) {",
             depth, depth, dimSizePrefix(dim.size).c_str(), dim.size.c_str(), depth);

        _encodeRecursive(zm, depth+1, extraIndent);

        emit(indent, "}");
    }

    void emitEncodeNohash()
    {
        const char* sn = zs.structname.shortname.c_str();
        if(zs.members.size() == 0) {
            emit(0, "int %s::_encodeNoHash(void* , uint32_t, uint32_t) const", sn);
            emit(0, "{");
            emit(1,     "return 0;");
            emit(0, "}");
            emit(0, "");
            return;
        }
        emit(0, "int %s::_encodeNoHash(void* buf, uint32_t offset, uint32_t maxlen) const", sn);
        emit(0, "{");
        emit(1,     "uint32_t pos = 0;");
        emit(1,     "int thislen;");
        emit(0, "");
        for (auto& zm : zs.members) {

            auto& mtn = zm.type.fullname;
            auto* mn = zm.membername.c_str();

            int ndims = (int)zm.dimensions.size();
            if (ndims == 0) {
                if (ZCMGen::isPrimitiveType(mtn)) {
                    if(mtn == "string") {
                        emit(1, "char* %s_cstr = (char*) this->%s.c_str();", mn, mn);
                        emit(1, "thislen = __string_encode_%sarray(buf, offset + pos, maxlen - pos, &%s_cstr, 1);",
                                zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                                mn);
                    } else {
                        emit(1, "thislen = __%s_encode_%sarray(buf, offset + pos, maxlen - pos, &this->%s, 1);",
                             mtn.c_str(),
                             zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                             mn);
                    }
                    emit(1, "if(thislen < 0) return thislen; else pos += thislen;");
                } else {
                    _encodeRecursive(zm, 0, 0);
                }
            } else {
                auto& lastDim = zm.dimensions[ndims-1];

                // for non-string primitive types with variable size final
                // dimension, add an optimization to only call the primitive encode
                // functions only if the final dimension size is non-zero.
                if (ZCMGen::isPrimitiveType(mtn) && mtn != "string" && !isDimSizeFixed(lastDim.size)) {
                    emit(1, "if(%s%s > 0) {", dimSizePrefix(lastDim.size).c_str(), lastDim.size.c_str());
                    _encodeRecursive(zm, 0, 1);
                    emit(1, "}");
                } else {
                    _encodeRecursive(zm, 0, 0);
                }
            }

            emit(0,"");
        }
        emit(1, "return pos;");
        emit(0,"}");
        emit(0,"");
    }

    void emitEncodedSizeNohash()
    {
        const char* sn = zs.structname.shortname.c_str();
        emit(0, "uint32_t %s::_getEncodedSizeNoHash() const", sn);
        emit(0, "{");
        if(zs.members.size() == 0) {
            emit(1,     "return 0;");
            emit(0,"}");
            emit(0,"");
            return;
        }
        emit(1,     "uint32_t enc_size = 0;");
        for (auto& zm : zs.members) {
            auto& mtn = zm.type.fullname;
            auto* mn = zm.membername.c_str();
            int ndim = (int)zm.dimensions.size();

            if (ZCMGen::isPrimitiveType(mtn) && mtn != "string") {
                emitStart(1, "enc_size += ");
                for(int n = 0; n < ndim-1; ++n) {
                    auto& dim = zm.dimensions[n];
                    emitContinue("%s%s * ", dimSizePrefix(dim.size).c_str(), dim.size.c_str());
                }
                if(ndim > 0) {
                    auto& dim = zm.dimensions[ndim-1];
                    emitEnd("__%s_encoded_array_size(NULL, %s%s);",
                            mtn.c_str(), dimSizePrefix(dim.size).c_str(), dim.size.c_str());
                } else {
                    emitEnd("__%s_encoded_array_size(NULL, 1);", mtn.c_str());
                }
            } else {
                for(int n = 0; n < ndim; ++n) {
                    auto& dim = zm.dimensions[n];
                    emit(1+n, "for (int a%d = 0; a%d < %s%s; ++a%d) {",
                         n, n, dimSizePrefix(dim.size).c_str(), dim.size.c_str(), n);
                }
                emitStart(ndim + 1, "enc_size += this->%s", mn);
                for(int i = 0; i < ndim; ++i)
                    emitContinue("[a%d]", i);
                if (mtn == "string") {
                    emitEnd(".size() + ZCM_CORETYPES_INT32_NUM_BYTES_ON_BUS + ZCM_CORETYPES_INT8_NUM_BYTES_ON_BUS;");
                } else {
                    emitEnd("._getEncodedSizeNoHash();");
                }
                for(int n = ndim-1; n >= 0; --n) {
                    emit(1 + n, "}");
                }
            }
        }
        emit(1, "return enc_size;");
        emit(0,"}");
        emit(0,"");
    }

    void _decodeRecursive(const ZCMMember& zm, int depth)
    {
        auto& mtn = zm.type.fullname;
        auto* mn = zm.membername.c_str();

        int ndims = (int)zm.dimensions.size();
        // primitive array
        if (depth+1 == ndims &&
            ZCMGen::isPrimitiveType(mtn) && mtn != "string") {

            auto& dim = zm.dimensions[depth];
            int decodeIndent = 1 + depth;
            if(!zm.isConstantSizeArray()) {
                emit(1 + depth, "if(%s > 0) {", dimSizeAccessor(dim.size).c_str());
                emitStart(2 + depth, "this->%s", mn);
                for(int i = 0; i < depth; ++i)
                    emitContinue("[a%d]", i);
                emitEnd(".resize(%s);", dimSizeAccessor(dim.size).c_str());
                ++decodeIndent;
            }

            emitStart(decodeIndent, "thislen = __%s_decode_%sarray(buf, offset + pos, maxlen - pos, &this->%s",
                                    mtn.c_str(),
                                    zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                                    mn);
            for(int i = 0; i < depth; ++i)
                emitContinue("[a%d]", i);
            emitEnd("[0], %s);", dimSizeAccessor(dim.size).c_str());
            emit(decodeIndent, "if(thislen < 0) return thislen; else pos += thislen;");
            if (!zm.isConstantSizeArray()) {
                emit(1 + depth, "}");
            }
        } else if(depth == ndims) {
            if (mtn == "string") {
                emit(1 + depth, "int32_t __elem_len;");
                emit(1 + depth, "thislen = __int32_t_decode_%sarray(buf, offset + pos, maxlen - pos, &__elem_len, 1);",
                                zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "");
                emit(1 + depth, "if(thislen < 0) return thislen; else pos += thislen;");
                emit(1 + depth, "if((uint32_t)__elem_len > maxlen - pos) return -1;");
                emitStart(1 + depth, "this->%s", mn);
                for(int i = 0; i < depth; ++i)
                    emitContinue("[a%d]", i);
                emitEnd(".assign(((const char*)buf) + offset + pos, __elem_len - ZCM_CORETYPES_INT8_NUM_BYTES_ON_BUS);");
                emit(1 + depth, "pos += __elem_len;");
            } else {
                emitStart(1 + depth, "thislen = this->%s", mn);
                for(int i = 0; i < depth; ++i)
                    emitContinue("[a%d]", i);
                emitEnd("._decodeNoHash(buf, offset + pos, maxlen - pos);");
                emit(1 + depth, "if(thislen < 0) return thislen; else pos += thislen;");
            }
        } else {
            auto& dim = zm.dimensions[depth];
            if(!zm.isConstantSizeArray()) {
                emitStart(1+depth, "this->%s", mn);
                for(int i = 0; i < depth; ++i) {
                    emitContinue("[a%d]", i);
                }
                emitEnd(".resize(%s);", dimSizeAccessor(dim.size).c_str());
            }
            emit(1+depth, "for (int a%d = 0; a%d < %s; ++a%d) {",
                 depth, depth, dimSizeAccessor(dim.size).c_str(), depth);

            _decodeRecursive(zm, depth+1);

            emit(1+depth, "}");
        }
    }

    void emitDecodeNohash()
    {
        const char* sn = zs.structname.shortname.c_str();
        if (zs.members.size() == 0) {
            emit(0, "int %s::_decodeNoHash(const void* , uint32_t, uint32_t)", sn);
            emit(0, "{");
            emit(1,     "return 0;");
            emit(0, "}");
            emit(0, "");
            return;
        }
        emit(0, "int %s::_decodeNoHash(const void* buf, uint32_t offset, uint32_t maxlen)", sn);
        emit(0, "{");
        emit(1,     "uint32_t pos = 0;");
        emit(1,     "int thislen;");
        emit(0, "");
        for (auto& zm : zs.members) {
            auto& mtn = zm.type.fullname;
            auto* mn = zm.membername.c_str();

            int ndims = (int)zm.dimensions.size();
            if (ndims == 0 && ZCMGen::isPrimitiveType(mtn)) {
                if(mtn == "string") {
                    emit(1, "int32_t __%s_len__;", mn);
                    emit(1, "thislen = __int32_t_decode_%sarray(buf, offset + pos, maxlen - pos, &__%s_len__, 1);",
                            zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                            mn);
                    emit(1, "if(thislen < 0) return thislen; else pos += thislen;");
                    emit(1, "if((uint32_t)__%s_len__ > maxlen - pos) return -1;", mn);
                    emit(1, "this->%s.assign(((const char*)buf) + offset + pos, __%s_len__ - ZCM_CORETYPES_INT8_NUM_BYTES_ON_BUS);", mn, mn);
                    emit(1, "pos += __%s_len__;", mn);
                } else {
                    emit(1, "thislen = __%s_decode_%sarray(buf, offset + pos, maxlen - pos, &this->%s, 1);",
                            mtn.c_str(),
                            zcm.gopt->getBool("little-endian-encoding") ? "little_endian_" : "",
                            mn);
                    emit(1, "if(thislen < 0) return thislen; else pos += thislen;");
                }
            } else {
                _decodeRecursive(zm, 0);
            }

            emit(0,"");
        }
        emit(1, "return pos;");
        emit(0, "}");
        emit(0, "");
    }

    void emitHeader()
    {
        emitHeaderStart();
        emitEncode();
        emitDecode();
        emitEncodedSize();
        emitGetHash();
        emitGetTypeName();
        emitEncodeNohash();
        emitDecodeNohash();
        emitEncodedSizeNohash();
        emitComputeHash();
        emitHeaderEnd();
    }
};

int emitCpp(const ZCMGen& zcm)
{
    // iterate through all defined message types
    for (auto& zs : zcm.structs) {
        string tn = dotsToSlashes(zs.structname.fullname);

        // compute the target filename
        string hpath = zcm.gopt->getString("cpp-hpath");
        string headerName = hpath + (hpath.size() > 0 ? "/" : ":") + tn +".hpp";

        // generate code if needed
        if (zcm.needsGeneration(zs.zcmfile, headerName)) {
            FileUtil::makeDirsForFile(headerName);
            Emit E{zcm, zs, headerName};
            if (!E.good())
                return -1;
            E.emitHeader();

        }
    }

    return 0;
}

vector<string> getFilepathsCpp(const ZCMGen& zcm)
{
    vector<string> ret;

    for (auto& zs : zcm.structs) {
        string tn = dotsToSlashes(zs.structname.fullname);

        // compute the target filename
        string hpath = zcm.gopt->getString("cpp-hpath");
        string headerName = hpath + (hpath.size() > 0 ? "/" : ":") + tn +".hpp";

        ret.push_back(headerName);
    }

    return ret;
}

unordered_set<string> getReservedKeywordsCpp()
{
    return { "alignas", "alignof", "and", "and_eq", "asm",
             "atomic_cancel", "atomic_commit", "atomic_noexcept", "auto",
             "bitand", "bitor", "bool", "break", "case", "catch",
             "char", "char16_t", "char32_t", "class", "compl", "concept",
             "const", "constexpr", "const_cast", "continue", "co_await",
             "co_return", "co_yield", "decltype", "default", "delete", "do",
             "double", "dynamic_cast", "else", "enum", "explicit", "export",
             "extern", "false", "float", "for", "friend", "goto", "if",
             "import", "inline", "int", "long", "module", "mutable", "namespace",
             "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
             "or_eq", "private", "protected", "public", "register",
             "reinterpret_cast", "requires", "return", "short", "signed",
             "sizeof", "static", "static_assert", "static_cast", "struct",
             "switch", "synchronized", "template", "this", "thread_local",
             "throw", "true", "try", "typedef", "typeid", "typename", "union",
             "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
             "while", "xor", "xor_eq" };
}
