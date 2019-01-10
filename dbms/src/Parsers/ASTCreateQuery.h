#pragma once

#include <Parsers/ASTExpressionList.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTSetQuery.h>
#include <Parsers/ASTSelectWithUnionQuery.h>
#include <Parsers/ASTQueryWithTableAndOutput.h>
#include <Parsers/ASTQueryWithOnCluster.h>


namespace DB
{

class ASTStorage : public IAST
{
public:
    ASTFunction * engine = nullptr;
    IAST * partition_by = nullptr;
    IAST * primary_key = nullptr;
    IAST * order_by = nullptr;
    IAST * sample_by = nullptr;
    ASTSetQuery * settings = nullptr;

    String getID(char) const override { return "Storage definition"; }

    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTStorage>(*this);
        res->children.clear();

        if (engine)
            res->set(res->engine, engine->clone());
        if (partition_by)
            res->set(res->partition_by, partition_by->clone());
        if (primary_key)
            res->set(res->primary_key, primary_key->clone());
        if (order_by)
            res->set(res->order_by, order_by->clone());
        if (sample_by)
            res->set(res->sample_by, sample_by->clone());
        if (settings)
            res->set(res->settings, settings->clone());

        return res;
    }

    void formatImpl(const FormatSettings & s, FormatState & state, FormatStateStacked frame) const override
    {
        if (engine)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "ENGINE" << (s.hilite ? hilite_none : "") << " = ";
            engine->formatImpl(s, state, frame);
        }
        if (partition_by)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "PARTITION BY " << (s.hilite ? hilite_none : "");
            partition_by->formatImpl(s, state, frame);
        }
        if (primary_key)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "PRIMARY KEY " << (s.hilite ? hilite_none : "");
            primary_key->formatImpl(s, state, frame);
        }
        if (order_by)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "ORDER BY " << (s.hilite ? hilite_none : "");
            order_by->formatImpl(s, state, frame);
        }
        if (sample_by)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "SAMPLE BY " << (s.hilite ? hilite_none : "");
            sample_by->formatImpl(s, state, frame);
        }
        if (settings)
        {
            s.ostr << (s.hilite ? hilite_keyword : "") << s.nl_or_ws << "SETTINGS " << (s.hilite ? hilite_none : "");
            settings->formatImpl(s, state, frame);
        }

    }
};


class ASTSource : public IAST
{
public:
    ASTFunction * source = nullptr;
    String password;
    UInt16 port;
    String user;
    ASTFunction * replica = nullptr;
    String database;
    String table;
    ASTFunction * lifetime;
    ASTFunction * layout;
    IAST * primary_key;

    String getID(char delimiter) const override;

    ASTPtr clone() const override;

    void formatImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override;
};


/// CREATE TABLE or ATTACH TABLE query or CREATE DICTIONARY or CREATE DATABASE
class ASTCreateQuery : public ASTQueryWithTableAndOutput, public ASTQueryWithOnCluster
{
public:
    bool attach{false};    /// Query ATTACH TABLE, not CREATE TABLE.
    bool replace{false};   /// Query CREATE OR REPLACE {TABLE,DICTIONARY}.
    bool if_not_exists{false};
    bool is_view{false};
    bool is_materialized_view{false};
    bool is_populate{false};
    ASTExpressionList * columns = nullptr;
    String to_database;   /// For CREATE MATERIALIZED VIEW mv TO table.
    String to_table;
    ASTStorage * storage = nullptr;
    String as_database;
    String as_table;
    ASTSelectWithUnionQuery * select = nullptr;
    ASTSource * dictionary_source = nullptr;


    /** Get the text that identifies this element. */
    String getID(char delim) const override { return (attach ? "AttachQuery" : "CreateQuery") + (delim + database) + delim + table; }

    ASTPtr clone() const override
    {
        auto res = std::make_shared<ASTCreateQuery>(*this);
        res->children.clear();

        if (columns)
            res->set(res->columns, columns->clone());
        if (storage)
            res->set(res->storage, storage->clone());
        if (select)
            res->set(res->select, select->clone());
        if (dictionary_source)
            res->set(res->dictionary_source, dictionary_source->clone());

        cloneOutputOptions(*res);

        return res;
    }

    ASTPtr getRewrittenASTWithoutOnCluster(const std::string & new_database) const override
    {
        return removeOnCluster<ASTCreateQuery>(clone(), new_database);
    }

protected:
    void formatQueryImpl(const FormatSettings & settings, FormatState & state, FormatStateStacked frame) const override
    {
        frame.need_parens = false;

        if (!database.empty() && table.empty() && dictionary.empty())
        {
            settings.ostr << (settings.hilite ? hilite_keyword : "")
                << (attach ? "ATTACH DATABASE " : "CREATE DATABASE ")
                << (if_not_exists ? "IF NOT EXISTS " : "")
                << (settings.hilite ? hilite_none : "")
                << backQuoteIfNeed(database);
            formatOnCluster(settings);

            if (storage)
                storage->formatImpl(settings, state, frame);

            return;
        }

        if (!table.empty())
        {
            std::string what = "TABLE";
            if (is_view)
                what = "VIEW";
            if (is_materialized_view)
                what = "MATERIALIZED VIEW";

            settings.ostr
                << (settings.hilite ? hilite_keyword : "")
                    << (attach ? "ATTACH " : "CREATE ")
                    << (temporary ? "TEMPORARY " : "")
                    << what << " "
                    << (if_not_exists ? "IF NOT EXISTS " : "")
                << (settings.hilite ? hilite_none : "")
                << (!database.empty() ? backQuoteIfNeed(database) + "." : "") << backQuoteIfNeed(table);
                formatOnCluster(settings);
        }

        if (!dictionary.empty())
        {
            settings.ostr
                << (settings.hilite ? hilite_keyword : "")
                << "CREATE DICTIONARY "
                << (if_not_exists ? "IF NOT EXISTS " : "")
                << (settings.hilite ? hilite_none : "")
                << (!database.empty() ? backQuoteIfNeed(database) + "." : "") << backQuoteIfNeed(dictionary);

            // TODO: нужно ли делать formatOnCluster(settings). ? Скорее всего нет
        }

        if (!to_table.empty())
        {
            settings.ostr
                << (settings.hilite ? hilite_keyword : "") << " TO " << (settings.hilite ? hilite_none : "")
                << (!to_database.empty() ? backQuoteIfNeed(to_database) + "." : "") << backQuoteIfNeed(to_table);
        }

        if (!as_table.empty())
        {
            settings.ostr
                << (settings.hilite ? hilite_keyword : "") << " AS " << (settings.hilite ? hilite_none : "")
                << (!as_database.empty() ? backQuoteIfNeed(as_database) + "." : "") << backQuoteIfNeed(as_table);
        }

        if (columns)
        {
            settings.ostr << (settings.one_line ? " (" : "\n(");
            FormatStateStacked frame_nested = frame;
            ++frame_nested.indent;
            columns->formatImpl(settings, state, frame_nested);
            settings.ostr << (settings.one_line ? ")" : "\n)");
        }

        if (storage)
            storage->formatImpl(settings, state, frame);

        if (is_populate)
        {
            settings.ostr << (settings.hilite ? hilite_keyword : "") << " POPULATE" << (settings.hilite ? hilite_none : "");
        }

        if (select)
        {
            settings.ostr << (settings.hilite ? hilite_keyword : "") << " AS" << settings.nl_or_ws << (settings.hilite ? hilite_none : "");
            select->formatImpl(settings, state, frame);
        }

        if (dictionary_source)
            dictionary_source->formatImpl(settings, state, frame);
    }
};

}
