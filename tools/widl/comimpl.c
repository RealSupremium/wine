/*
 * IDL Compiler
 *
 * Copyright 2002 Ove Kaaven
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

#include "widl.h"
#include "utils.h"
#include "parser.h"
#include "header.h"
#include "expr.h"
#include "typetree.h"
#include "typelib.h"

static int indent;

static char *strupper( char *s )
{
    for (char *p = s; *p; p++) *p = toupper( *p );
    return s;
}

static void write_import( const char *import )
{
    char *header = replace_extension( get_basename( import ), ".idl", ".h" );
    put_str( indent, "#include <%s>\n", header );
    free( header );
}

static void write_imports( const statement_list_t *stmts )
{
    const statement_t *stmt;

    if (stmts) LIST_FOR_EACH_ENTRY( stmt, stmts, const statement_t, entry )
    {
        switch (stmt->type)
        {
        case STMT_IMPORT: write_import( stmt->u.str ); break;
        case STMT_LIBRARY: write_imports( stmt->u.lib->stmts ); break;
        case STMT_TYPE:
            if (type_get_type( stmt->u.type ) != TYPE_INTERFACE) break;
            write_imports( type_iface_get_stmts( stmt->u.type ) );
            break;
        default: break;
        }
    }
}

static bool iface_inherits( const type_t *iface, const char *name )
{
    if (!iface || !strcmp( iface->name, name )) return !!iface;
    return iface_inherits( type_iface_get_inherit( iface ), name );
}

static const char *get_trace_wrapper( const char *name, const decl_spec_t *decl_spec )
{
    type_t *type = decl_spec->type;

    switch (type_get_type_detect_alias( type ))
    {
    case TYPE_BASIC: break;
    case TYPE_ALIAS:
        if (type->name && !strcmp( type->name, "HSTRING" )) return strmake( "debugstr_hstring(%s)", name );
        if (type->name && strstr( type->name, "CWSTR" )) return strmake( "debugstr_w(%s)", name );
        if (type->name && strstr( type->name, "CSTR" )) return strmake( "debugstr_a(%s)", name );
        if (type->name && !strcmp( type->name, "REFIID" )) return strmake( "debugstr_guid(%s)", name );
        if (type->name && !strcmp( type->name, "GUID" )) return strmake( "debugstr_guid(&(%s))", name );
        return get_trace_wrapper( name, type_alias_get_aliasee( type ) );
    case TYPE_ENUM: return name;
    case TYPE_STRUCT:
    case TYPE_ENCAPSULATED_UNION:
    case TYPE_UNION:
        return strmake( "&%s", name );
    case TYPE_FUNCTION: break;
    case TYPE_INTERFACE: break;
    case TYPE_POINTER: break;
    case TYPE_ARRAY: break;
    case TYPE_DELEGATE: break;

    case TYPE_VOID:
    case TYPE_MODULE:
    case TYPE_COCLASS:
    case TYPE_BITFIELD:
    case TYPE_APICONTRACT:
    case TYPE_RUNTIMECLASS:
    case TYPE_PARAMETERIZED_TYPE:
    case TYPE_PARAMETER:
        assert( 0 );
        break;
    }

    return name;
}

static const char *get_format_specifier( const char *name, const decl_spec_t *decl_spec )
{
    type_t *type = decl_spec->type;

    switch (type_get_type_detect_alias( type ))
    {
    case TYPE_BASIC:
    {
        int flags = !!strcasestr( name, "flags" ), sign = type_basic_get_sign( type ) <= 0;
        switch (type_basic_get_type( type ))
        {
        case TYPE_BASIC_INT8:           return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_INT16:          return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_INT:            return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_INT3264:        return flags ? "%#Ix"   : sign ? "%Id"   : "%Iu";
        case TYPE_BASIC_BYTE:           return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_CHAR:           return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_WCHAR:          return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_FLOAT:          return flags ? "%#x"    : sign ? "%f"    : "%u";
        case TYPE_BASIC_DOUBLE:         return flags ? "%#x"    : sign ? "%f"    : "%u";
        case TYPE_BASIC_ERROR_STATUS_T: return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_HANDLE:         return flags ? "%#x"    : sign ? "%p"    : "%u";
        case TYPE_BASIC_INT32:          return flags ? "%#x"    : sign ? "%d"    : "%u";
        case TYPE_BASIC_LONG:           return flags ? "%#lx"   : sign ? "%ld"   : "%lu";
        case TYPE_BASIC_INT64:          return flags ? "%#I64x" : sign ? "%I64d" : "%I64u";
        case TYPE_BASIC_HYPER:          return flags ? "%#I64x" : sign ? "%I64d" : "%I64u";
        }
        break;
    }
    case TYPE_ALIAS:
        if (type->name && !strcmp( type->name, "HSTRING" )) return "%s";
        if (type->name && strstr( type->name, "CWSTR" )) return "%s";
        if (type->name && strstr( type->name, "CSTR" )) return "%s";
        if (type->name && !strcmp( type->name, "GUID" )) return "%s";
        if (type->name && !strcmp( type->name, "REFIID" )) return "%s";
        return get_format_specifier( name, type_alias_get_aliasee( type ) );
    case TYPE_ENUM:               return "%#x";
    case TYPE_STRUCT:             return "%p";
    case TYPE_ENCAPSULATED_UNION: return "%p";
    case TYPE_UNION:              return "%p";
    case TYPE_FUNCTION:           return "%p";
    case TYPE_INTERFACE:          return "%p";
    case TYPE_POINTER:            return "%p";
    case TYPE_ARRAY:              return "%p";
    case TYPE_DELEGATE:           return "%p";

    case TYPE_VOID:
    case TYPE_MODULE:
    case TYPE_COCLASS:
    case TYPE_BITFIELD:
    case TYPE_APICONTRACT:
    case TYPE_RUNTIMECLASS:
    case TYPE_PARAMETERIZED_TYPE:
    case TYPE_PARAMETER:
        assert( 0 );
        break;
    }

    return NULL;
}

static void append_method_args_decl( struct strbuf *str, const var_list_t *args )
{
    const var_t *arg;
    if (args) LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
    {
        strappend( str, ", " );
        append_declspec( str, &arg->declspec, NAME_C, "WINAPI", false, arg->name );
    }
}

static void append_method_args_call( struct strbuf *str, const var_list_t *args, bool debug )
{
    const var_t *arg;

    if (args) LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
    {
        if (debug) strappend( str, ", %s", get_trace_wrapper( arg->name, &arg->declspec ) );
        else strappend( str, ", %s", arg->name );
    }
}

static void append_method_decl_klass( struct strbuf *str, const type_t *impl, const type_t *iface, const var_t *func )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const decl_spec_t *ret = type_function_get_ret( func->declspec.type );

    strappend( str, "static " );
    append_type_left( str, ret, NAME_C, NULL );
    strappend( str, " WINAPI %s_%s_%s( ", impl->name, short_name, get_name( func ) );
    strappend( str, "%s *iface", iface->c_name );
    append_method_args_decl( str, type_function_get_args( func->declspec.type ) );
    strappend( str, " )" );
}

static void append_method_decl_impl( struct strbuf *str, const type_t *impl, const var_t *func )
{
    const decl_spec_t *ret = type_function_get_ret( func->declspec.type );

    strappend( str, "    " );
    append_type_left( str, ret, NAME_C, NULL );
    strappend( str, " (*%s)( ", get_name( func ) );
    strappend( str, "struct %s *impl", impl->name );
    append_method_args_decl( str, type_function_get_args( func->declspec.type ) );
    strappend( str, " )" );
}

static void append_method_args_debug_format( struct strbuf *str, const var_list_t *args )
{
    const var_t *arg;

    if (args) LIST_FOR_EACH_ENTRY( arg, args, const var_t, entry )
    {
        strappend( str, " %s %s", arg->name, get_format_specifier( arg->name, &arg->declspec ) );
    }
}

static void write_iface_methods( const type_t *klass, const type_t *impl, const char *impl_prefix, const type_t *iface, const type_t *top_iface )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const statement_t *stmt;
    struct strbuf str = {0};
    const type_t *base;

    if ((base = type_iface_get_inherit( iface ))) write_iface_methods( klass, impl, impl_prefix, base, top_iface );
    if (!strcmp( iface->name, "IUnknown" )) return;
    if (!strcmp( iface->name, "IInspectable" )) return;
    if (!strcmp( iface->name, "IClassFactory" )) return;

    STATEMENTS_FOR_EACH_FUNC( stmt, type_iface_get_stmts( iface ) )
    {
        const var_t *func = stmt->u.var;
        decl_spec_t ret = *type_function_get_ret( func->declspec.type );
        bool is_void = ret.type->type_type == TYPE_VOID;

        if (is_override_method( iface, top_iface, func )) continue;
        if (is_callas( func->attrs )) continue;

        append_method_decl_klass( &str, impl, top_iface, func );
        put_str( indent, "%s\n", str.buf );
        str.pos = 0;

        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );

        if (!is_void)
        {
            append_declspec( &str, &ret, NAME_C, "WINAPI", false, "ret" );
            put_str( indent, "    %s;\n", str.buf );
            str.pos = 0;
        }

        strappend( &str, "\"%s %%p", impl->name );
        append_method_args_debug_format( &str, type_function_get_args(func->declspec.type) );
        strappend( &str, "\\n\", impl" );
        append_method_args_call( &str, type_function_get_args(func->declspec.type), true );
        put_str( indent, "    TRACE( %s );\n", str.buf );
        str.pos = 0;

        strappend( &str, "impl" );
        append_method_args_call( &str, type_function_get_args( func->declspec.type ), false );
        put_str( indent, "    %s%s_funcs.%s( %s );\n", is_void ? "" : "ret = ", impl_prefix, get_name( func ), str.buf );
        str.pos = 0;

        strappend( &str, "\"%s %%p", impl->name );
        if (is_void || !ret.type->name || strcmp( ret.type->name, "HRESULT" ))
        {
            strappend( &str, "\\n\", impl" );
            put_str( indent, "    TRACE( %s );\n", str.buf );
        }
        else
        {
            strappend( &str, " -> %%#lx\\n\", impl" );
            put_str( indent, "    if (FAILED(ret)) WARN( %s, ret );\n", str.buf );
            put_str( indent, "    else TRACE( %s, ret );\n", str.buf );
        }
        str.pos = 0;

        if (!is_void) put_str( indent, "    return ret;\n" );
        put_str( indent, "}\n" );
    }
}

static void write_iface_vtable_methods( const type_t *impl, const type_t *iface, const type_t *top_iface )
{
    const char *short_name = top_iface->short_name ? top_iface->short_name : top_iface->name;
    const statement_t *stmt;
    const type_t *base;

    if ((base = type_iface_get_inherit( iface ))) write_iface_vtable_methods( impl, base, top_iface );

    STATEMENTS_FOR_EACH_FUNC( stmt, type_iface_get_stmts( iface ) )
    {
        const var_t *func = stmt->u.var;

        if (is_override_method( iface, top_iface, func )) continue;
        if (is_callas( func->attrs )) continue;

        put_str( indent, "%s_%s_%s,\n", impl->name, short_name, get_name( func ) );
    }
}

static void write_iface_vtable( const type_t *impl, const type_t *iface )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;

    put_str( indent, "static const struct %sVtbl %s_%s_vtbl =\n", iface->c_name, impl->name, short_name );
    put_str( indent++, "{\n" );
    write_iface_vtable_methods( impl, iface, iface );
    put_str( --indent, "};\n" );
}

static void write_default_IUnknown( const type_t *klass, const type_t *impl, const type_t *iface, const char *refcount )
{
    const char *query_name, *short_name = iface->short_name ? iface->short_name : iface->name;
    const char *prefix = strmake( "%s_%s", impl->name, short_name );
    bool sealed = false;
    const var_t *var;

    if (!iface_inherits( iface, "IUnknown" )) return;
    if (iface_inherits( iface, "IClassFactory" )) sealed = true;

    put_str( indent, "static HRESULT WINAPI %s_QueryInterface( %s *iface, REFIID iid, void **out )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
    put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
    put_str( indent, "    HRESULT hr;\n" );
    put_str( indent, "    TRACE( \"%s %%p iid %%s out %%p\\n\", impl, debugstr_guid(iid), out );\n", impl->name );
    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        query_name = var->declspec.type->short_name ? var->declspec.type->short_name : var->declspec.type->name;
        put_str( indent, "    if (SUCCEEDED(hr = %s_query_%s( klass, iid, out ))) return hr;\n", klass->name, query_name );
    }
    if (!sealed) put_str( indent, "    return %s_funcs.missing_interface( impl, iid, out );\n", impl->name );
    else
    {
        put_str( indent, "    *out = NULL;\n" );
        put_str( indent, "    return E_NOINTERFACE;\n" );
    }
    put_str( indent, "}\n" );

    if (!refcount)
    {
        put_str( indent, "static ULONG WINAPI %s_AddRef( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    return 2;\n" );
        put_str( indent, "}\n" );
        put_str( indent, "static ULONG WINAPI %s_Release( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    return 1;\n" );
        put_str( indent, "}\n" );
    }
    else
    {
        put_str( indent, "static ULONG WINAPI %s_AddRef( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
        put_str( indent, "    LONG ref = InterlockedIncrement( &klass->%s );\n", refcount );
        put_str( indent, "    TRACE( \"%s %%p increasing refcount to %%ld\\n\", impl, ref );\n", impl->name );
        put_str( indent, "    return ref;\n" );
        put_str( indent, "}\n" );
        put_str( indent, "static ULONG WINAPI %s_Release( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
        put_str( indent, "    LONG ref = InterlockedDecrement( &klass->%s );\n", refcount );
        put_str( indent, "    TRACE( \"%s %%p decreasing refcount to %%ld\\n\", impl, ref);\n", impl->name );
        put_str( indent, "    if (!ref) %s_funcs.destroy( impl );\n", impl->name );
        put_str( indent, "    return ref;\n" );
        put_str( indent, "}\n" );
    }
}

static void write_default_IInspectable( const type_t *klass, const type_t *impl, const type_t *iface, const char *class_name )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const char *prefix = strmake( "%s_%s", impl->name, short_name );

    if (!iface_inherits( iface, "IInspectable" )) return;

    put_str( indent, "static HRESULT WINAPI %s_GetIids( %s *iface, ULONG *count, IID **iids )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
    put_str( indent, "    FIXME( \"%s %%p count %%p iids %%p stub!\\n\", impl, count, iids );\n", impl->name );
    put_str( indent, "    return E_NOTIMPL;\n" );
    put_str( indent, "}\n" );
    put_str( indent, "static HRESULT WINAPI %s_GetRuntimeClassName( %s *iface, HSTRING *class_name )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
    if (class_name)
    {
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    TRACE( \"%s %%p class_name %%p\\n\", impl, class_name );\n", impl->name );
        put_str( indent, "    if (!klass->%s) return WindowsDuplicateString( NULL, class_name );\n", class_name );
        put_str( indent, "    return WindowsCreateString( klass->%s, wcslen( klass->%s ), class_name );\n", class_name, class_name );
    }
    else
    {
        put_str( indent, "    FIXME( \"%s %%p class_name %%p stub!\\n\", impl, class_name );\n", impl->name );
        put_str( indent, "    return E_NOTIMPL;\n" );
    }
    put_str( indent, "}\n" );
    put_str( indent, "static HRESULT WINAPI %s_GetTrustLevel( %s *iface, TrustLevel *trust_level )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
    put_str( indent, "    FIXME( \"%s %%p trust_level %%p stub!\\n\", impl, trust_level );\n", impl->name );
    put_str( indent, "    return E_NOTIMPL;\n" );
    put_str( indent, "}\n" );
}

static void write_default_IClassFactory( const type_t *klass, const type_t *impl, const type_t *iface, const char *class_name )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const char *prefix = strmake( "%s_%s", impl->name, short_name );

    if (!iface_inherits( iface, "IClassFactory" )) return;

    put_str( indent, "static HRESULT WINAPI %s_CreateInstance( %s *iface, IUnknown *outer, REFIID riid, void **out )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    struct %s *impl = %s_from_%s( iface );\n", impl->name, impl->name, short_name );
    put_str( indent, "    TRACE( \"%s %%p outer %%p riid %%s out %%p\\n\", impl, outer, debugstr_guid(riid), out );\n", impl->name );
    put_str( indent, "    return %s_funcs.create_instance( outer, riid, out );\n", impl->name );
    put_str( indent, "}\n" );
    put_str( indent, "static HRESULT WINAPI %s_LockServer( %s *iface, BOOL lock )\n", prefix, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    return S_OK;\n" );
    put_str( indent, "}\n" );
}

static void write_iface_forwarding( const type_t *klass, const type_t *impl, const type_t *iface, const type_t *default_iface,
                                    const char *forward, const type_t *forward_iface )
{
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const char *prefix = strmake( "%s_%s", impl->name, short_name );

    if (iface_inherits( iface, "IUnknown" ) && iface_inherits( default_iface, "IUnknown" ))
    {
        put_str( indent, "static HRESULT WINAPI %s_QueryInterface( %s *iface, REFIID iid, void **out )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_QueryInterface( %s, iid, out );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
        put_str( indent, "static ULONG WINAPI %s_AddRef( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_AddRef( %s );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
        put_str( indent, "static ULONG WINAPI %s_Release( %s *iface )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_Release( %s );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
    }
    if (iface_inherits( iface, "IInspectable" ) && iface_inherits( default_iface, "IInspectable" ))
    {
        put_str( indent, "static HRESULT WINAPI %s_GetIids( %s *iface, ULONG *count, IID **iids )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_GetIids( %s, count, iids );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
        put_str( indent, "static HRESULT WINAPI %s_GetRuntimeClassName( %s *iface, HSTRING *class_name )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_GetRuntimeClassName( %s, class_name );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
        put_str( indent, "static HRESULT WINAPI %s_GetTrustLevel( %s *iface, TrustLevel *trust_level )\n", prefix, iface->c_name );
        put_str( indent, "{\n" );
        put_str( indent, "    struct %s *klass = %s_from_%s( iface );\n", klass->name, klass->name, short_name );
        put_str( indent, "    return %s_GetTrustLevel( %s, trust_level );\n", forward_iface->c_name, forward );
        put_str( indent, "}\n" );
    }
}

static void write_iface_impl_from( const type_t *klass, const type_t *impl, const var_t *var )
{
    const type_t *iface = var->declspec.type;
    const char *short_name = iface->short_name ? iface->short_name : iface->name;

    put_str( indent, "static inline struct %s *%s_from_%s( %s *iface )\n", klass->name, klass->name, short_name, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    return CONTAINING_RECORD( iface, struct %s, %s );\n", klass->name, var->name );
    put_str( indent, "}\n" );

    if (impl == klass) return;

    put_str( indent, "static inline struct %s *%s_from_%s( %s *iface )\n", impl->name, impl->name, short_name, iface->c_name );
    put_str( indent, "{\n" );
    put_str( indent, "    return %s_funcs.from_klass( %s_from_%s( iface ) );\n", impl->name, klass->name, short_name );
    put_str( indent, "}\n" );
}

static void write_iface_query( const type_t *klass, const type_t *impl, const var_t *var )
{
    const expr_t *iid_expr = get_attrp( var->attrs, ATTR_IIDIS );
    const type_t *iface = var->declspec.type;
    const char *short_name = iface->short_name ? iface->short_name : iface->name;
    const char *iid;

    if (iid_expr) iid = strmake( "klass->%s", iid_expr->u.sval );
    else iid = strmake( "&IID_%s", iface->c_name );

    if (!iface_inherits( iface, "IUnknown" )) return;

    put_str( indent, "static HRESULT %s_query_%s( struct %s *klass, REFIID iid, void **out )\n", klass->name, short_name, klass->name );
    put_str( indent, "{\n" );
    put_str( indent, "    if (!(klass)->%s.lpVtbl) return E_NOINTERFACE;\n", var->name );
    put_str( indent, "    if (IsEqualGUID( (iid), %s )", iid );
    for (const type_t *base = type_iface_get_inherit( iface ); base; (base = type_iface_get_inherit( base )))
        put_str( indent, "\n     || IsEqualGUID( (iid), &IID_%s )", base->c_name );
    put_str( indent, ")\n" );
    put_str( indent, "    {\n" );
    put_str( indent, "        %s_AddRef( &(klass)->%s );\n", iface->c_name, var->name );
    put_str( indent, "        *(out) = &(klass)->%s;\n", var->name );
    put_str( indent, "        return S_OK;\n" );
    put_str( indent, "    }\n" );
    put_str( indent, "    return E_NOINTERFACE;\n" );
    put_str( indent, "}\n" );
}

static void write_iface_methods_impl( const type_t *klass, const type_t *impl, const type_t *iface, const type_t *top_iface )
{
    const statement_t *stmt;
    struct strbuf str = {0};
    const type_t *base;

    if ((base = type_iface_get_inherit( iface ))) write_iface_methods_impl( klass, impl, base, top_iface );
    if (!strcmp( iface->name, "IUnknown" )) return;
    if (!strcmp( iface->name, "IInspectable" )) return;
    if (!strcmp( iface->name, "IClassFactory" )) return;

    STATEMENTS_FOR_EACH_FUNC( stmt, type_iface_get_stmts( iface ) )
    {
        const var_t *func = stmt->u.var;

        if (is_override_method( iface, top_iface, func )) continue;
        if (is_callas( func->attrs )) continue;

        append_method_decl_impl( &str, impl, func );
        put_str( indent, "%s;\n", str.buf );
        str.pos = 0;
    }
}

static void write_iface_methods_init( const type_t *klass, const type_t *impl, const type_t *iface, const type_t *top_iface )
{
    const statement_t *stmt;
    const type_t *base;

    if ((base = type_iface_get_inherit( iface ))) write_iface_methods_init( klass, impl, base, top_iface );
    if (!strcmp( iface->name, "IUnknown" )) return;
    if (!strcmp( iface->name, "IInspectable" )) return;
    if (!strcmp( iface->name, "IClassFactory" )) return;

    STATEMENTS_FOR_EACH_FUNC( stmt, type_iface_get_stmts( iface ) )
    {
        const var_t *func = stmt->u.var;

        if (is_override_method( iface, top_iface, func )) continue;
        if (is_callas( func->attrs )) continue;

        put_str( indent, ".%s = %s_%s, \\\n", get_name( func ), impl->name, get_name( func ) );
    }
}

static void write_impl( const type_t *impl, const type_t *klass )
{
    bool create_ins, from_klass, miss_iface, destroy;
    struct strbuf str = {0};
    const var_t *var, *def = NULL, *out = NULL, *ref = NULL, *cls = NULL;
    const type_t *forward_iface;
    const char *forward;

    put_str( indent, "#ifdef WIDL_impl_%s\n\n", impl->name );

    if (impl != klass) put_str( indent, "struct %s;\n", impl->name );
    put_str( indent, "struct %s\n", klass->name );
    put_str( indent++, "{\n" );
    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!def && strendswith( var->name, "_iface" )) def = var;
        if (!out && strendswith( var->name, "_outer" )) out = var;
        if (!ref && !strcmp( var->name, "ref" )) ref = var;
        if (!ref && !strcmp( var->name, "refcount" )) ref = var;
        if (!cls && !strcmp( var->name, "class_name" )) cls = var;
        append_declspec( &str, &var->declspec, NAME_C, "WINAPI", false, var->name );
        put_str( indent, "%s;\n", str.buf );
        str.pos = 0;
    }
    put_str( --indent, "};\n\n" );

    create_ins = def && iface_inherits( def->declspec.type, "IClassFactory" );
    miss_iface = def && iface_inherits( def->declspec.type, "IUnknown" ) && !create_ins;
    from_klass = impl != klass;
    destroy = ref != NULL;

    put_str( indent, "static const struct %s_funcs\n", impl->name );
    put_str( indent, "{\n" );
    if (from_klass) put_str( indent, "    struct %s *(*from_klass)( struct %s *klass );\n", impl->name, klass->name );
    if (miss_iface) put_str( indent, "    HRESULT (*missing_interface)( struct %s *%s, REFIID iid, void **out );\n", impl->name, impl->name );
    if (destroy)    put_str( indent, "    void (*destroy)( struct %s *impl );\n", impl->name );
    if (create_ins) put_str( indent, "    HRESULT (*create_instance)( IUnknown *outer, REFIID riid, void **out );\n" );
    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        write_iface_methods_impl( klass, impl, var->declspec.type, var->declspec.type );
    }
    put_str( indent, "    int placeholder;\n" );
    put_str( indent, "} %s_funcs;\n\n", impl->name );

    put_str( indent++, "#define %s_FUNCS_INIT \\\n", strupper( strdup( impl->name ) ) );
    put_str( indent++, "{ \\\n" );
    if (from_klass) put_str( indent, ".from_klass = %s_from_klass, \\\n", impl->name );
    if (miss_iface) put_str( indent, ".missing_interface = %s_missing_interface, \\\n", impl->name );
    if (destroy)    put_str( indent, ".destroy = %s_destroy, \\\n", impl->name );
    if (create_ins) put_str( indent, ".create_instance = %s_create_instance, \\\n", impl->name );
    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        write_iface_methods_init( klass, impl, var->declspec.type, var->declspec.type );
    }
    put_str( indent, "0 \\\n" );
    put_str( --indent, "}\n" );
    put_str( --indent, "\n" );

    if (out)
    {
        forward = strmake( "klass->%s", out->name );
        forward_iface = type_pointer_get_ref_type( out->declspec.type );
    }
    else
    {
        forward = strmake( "&klass->%s", def->name );
        forward_iface = def->declspec.type;
    }

    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        write_iface_impl_from( klass, impl, var );
    }
    put_str( indent, "\n" );

    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        write_iface_query( klass, impl, var );
    }
    put_str( indent, "\n" );

    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        if (!strendswith( var->name, "_iface" )) continue;
        if (var != def) write_iface_forwarding( klass, impl, var->declspec.type, def->declspec.type, forward, forward_iface );
        else
        {
            write_default_IUnknown( klass, impl, var->declspec.type, ref ? ref->name : NULL );
            write_default_IInspectable( klass, impl, var->declspec.type, cls ? cls->name : NULL );
            write_default_IClassFactory( klass, impl, var->declspec.type, cls ? cls->name : NULL );
        }
        write_iface_methods( klass, impl, impl->name, var->declspec.type, var->declspec.type );
        write_iface_vtable( impl, var->declspec.type );
    }
    put_str( indent, "\n" );

    put_str( indent, "static const struct %s %s_default =\n", klass->name, klass->name );
    put_str( indent, "{\n" );
    LIST_FOR_EACH_ENTRY( var, type_struct_get_fields( klass ), var_t, entry )
    {
        const type_t *iface = var->declspec.type;
        const char *short_name = iface->short_name ? iface->short_name : iface->name;
        const char *prefix = strmake( "%s_%s", impl->name, short_name );
        if (!strendswith( var->name, "_iface" )) continue;
        put_str( indent, "    .%s.lpVtbl = &%s_vtbl,\n", var->name, prefix );
    }
    if (ref) put_str( indent, "    .%s = 1,\n", ref->name );
    put_str( indent, "};\n\n" );

    put_str( indent, "static inline void %s_init( struct %s *klass%s )\n", klass->name, klass->name,
             cls ? ", const WCHAR *class_name" : "" );
    put_str( indent, "{\n" );
    put_str( indent, "    *klass = %s_default;\n", klass->name );
    if (cls) put_str( indent, "    klass->class_name = class_name;\n" );
    put_str( indent, "};\n\n" );

    put_str( indent, "#endif /* WIDL_impl_%s */\n", impl->name );
}

static void write_impls( const statement_list_t *stmts )
{
    const statement_t *stmt;

    LIST_FOR_EACH_ENTRY( stmt, stmts, const statement_t, entry )
    {
        const type_t *type = stmt->u.type, *impl;
        if (stmt->type != STMT_TYPE || !stmt->is_defined) continue;
        if (!(impl = get_attrp( type->attrs, ATTR_IMPL ))) continue;
        write_impl( impl, type );
    }
}

void write_comimpl( const statement_list_t *stmts )
{
    if (!do_comimpl) return;

    init_output_buffer();
    put_str( indent, "/*** Autogenerated by WIDL %s from %s - Do not edit ***/\n\n", PACKAGE_VERSION, input_name );
    write_imports( stmts );
    put_str( indent, "\n" );

    write_impls( stmts );
    flush_output_buffer( comimpl_name );
}
