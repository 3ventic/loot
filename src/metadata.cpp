/*  BOSS

    A plugin load order optimiser for games that use the esp/esm plugin system.

    Copyright (C) 2012    WrinklyNinja

    This file is part of BOSS.

    BOSS is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    BOSS is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with BOSS.  If not, see
    <http://www.gnu.org/licenses/>.
*/

#include "helpers.h"
#include "metadata.h"
#include "parsers.h"

#include <src/playground.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

using namespace std;

namespace boss {

    FormID::FormID() : id(0) {}
    
    FormID::FormID(const std::string& sourcePlugin, const uint32_t objectID) : plugin(sourcePlugin), id(objectID) {}
    
    FormID::FormID(const std::vector<std::string>& sourcePlugins, const uint32_t formID) {
        int index = formID >> 24;
        id = formID & ~((uint32_t)index << 24);

        if (index >= sourcePlugins.size()) {
            //cout << hex << formID << dec << " in " << sourcePlugins.back() << " has a higher modIndex than expected." << endl;
            index = sourcePlugins.size() - 1;
        }

        plugin = sourcePlugins[index];
    }

    bool FormID::operator == (const FormID& rhs) const {
        return (id == rhs.Id() && boost::iequals(plugin, rhs.Plugin()));
    }
    
    bool FormID::operator < (const FormID& rhs) const {
        if (id != rhs.Id())
            return id < rhs.Id();
        else
            return boost::ilexicographical_compare(plugin, rhs.Plugin());
    }
    
    std::string FormID::Plugin() const {
        return plugin;
    }
    
    uint32_t FormID::Id() const {
        return id;
    }

    ConditionalData::ConditionalData() {}

    ConditionalData::ConditionalData(const string& c) : condition(c) {}

    ConditionalData::ConditionalData(const std::string& c, const std::string& d)
        : condition(c), data(d) {}

    bool ConditionalData::IsConditional() const {
        return !condition.empty();
    }

    std::string ConditionalData::Condition() const {
        return condition;
    }

    std::string ConditionalData::Data() const {
        return data;
    }

    void ConditionalData::Data(const std::string& d) {
        data = d;
    }

    bool ConditionalData::EvalCondition(boss::Game& game) const {
        if (condition.empty())
            return true;

        boost::unordered_map<std::string, bool>::const_iterator it = game.conditionCache.find(boost::to_lower_copy(condition));
        if (it != game.conditionCache.end())
            return it->second;

        condition_grammar<std::string::const_iterator, boost::spirit::qi::space_type> grammar;
        boost::spirit::qi::space_type skipper;
        std::string::const_iterator begin, end;
        bool eval;

        grammar.SetGame(game);
        begin = condition.begin();
        end = condition.end();

        bool r;
        try {
            r = boost::spirit::qi::phrase_parse(begin, end, grammar, skipper, eval);
        } catch (boss::error& e) {
            throw boss::error(boss::ERROR_PATH_READ_FAIL, "Parsing of condition \"" + condition + "\" failed: " + e.what());
        }

        if (!r || begin != end)
            throw boss::error(boss::ERROR_PATH_READ_FAIL, "Parsing of condition \"" + condition + "\" failed!");

        game.conditionCache.emplace(boost::to_lower_copy(condition), eval);

        return eval;
    }

    Message::Message() {}
    
    Message::Message(const unsigned int type, const std::string& content,
                     const std::string& condition, const unsigned int language)
        : _type(type), _language(language), ConditionalData(condition, content) {}
        
    bool Message::operator < (const Message& rhs) const {
        return boost::ilexicographical_compare(Content(), rhs.Content());
    }

    bool Message::operator == (const Message& rhs) const {
        return (_type == rhs.Type() && boost::iequals(Content(), rhs.Content()));
    }

    bool Message::EvalCondition(boss::Game& game, const unsigned int lang) const {
        if (_language == LANG_AUTO || _language == lang)
            return ConditionalData::EvalCondition(game);
        else
            return false;
    }

    unsigned int Message::Type() const {
        return _type;
    }

    unsigned int Message::Language() const {
        return _language;
    }

    std::string Message::Content() const {
        return Data();
    }

    File::File() {}
    File::File(const std::string& name, const std::string& display, const std::string& condition)
        : _display(display), ConditionalData(condition, name) {}

    bool File::operator < (const File& rhs) const {
        return boost::ilexicographical_compare(Name(), rhs.Name());
    }

    bool File::operator == (const File& rhs) const {
        return boost::iequals(Name(), rhs.Name());
    }

    std::string File::Name() const {
        return Data();
    }

    std::string File::DisplayName() const {
        return _display;
    }

    Tag::Tag() : addTag(true) {}

    Tag::Tag(const string& tag, const bool isAddition, const string& condition) : addTag(isAddition), ConditionalData(condition, tag) {}

    bool Tag::operator < (const Tag& rhs) const {
        if (addTag != rhs.IsAddition())
            return (addTag && !rhs.IsAddition());
        else
            return boost::ilexicographical_compare(Name(), rhs.Name());
    }

    bool Tag::operator == (const Tag& rhs) const {
        return (addTag == rhs.IsAddition() && boost::iequals(Name(), rhs.Name()));
    }

    bool Tag::IsAddition() const {
        return addTag;
    }

    std::string Tag::Name() const {
        return Data();
    }

    Plugin::Plugin() : enabled(true), priority(0), isMaster(false) {}
    Plugin::Plugin(const std::string& n) : name(n), enabled(true), priority(0), isMaster(false) {}

	Plugin::Plugin(boss::Game& game, const std::string& n, const bool headerOnly)
		: name(n), enabled(true), priority(0) {
			
		// Get data from file contents using libespm. Assumes libespm has already been initialised.
		boost::filesystem::path filepath = game.DataPath() / n;
        espm::File file(filepath.string(), game.espm_settings, false, headerOnly);

		isMaster = file.isMaster(game.espm_settings);
		masters = file.getMasters();
		
		vector<uint32_t> records = file.getFormIDs();
        vector<string> plugins = masters;
        plugins.push_back(name);
		for (vector<uint32_t>::const_iterator it = records.begin(),endIt = records.end(); it != endIt; ++it)
			formIDs.insert(FormID(plugins, *it));

        //Also read Bash Tags applied and version string in description.
        for(size_t i=0,max=file.fields.size(); i < max; ++i){
            if (strncmp(file.fields[i].type,"SNAM", 4) == 0) {
                string text = file.fields[i].data;

                string::const_iterator begin, end;
                begin = text.begin();
                end = text.end(); 

                for(int j = 0; j < 7 && version.empty(); j++) {
                    boost::smatch what;
                    while (boost::regex_search(begin, end, what, version_checks[j])) {
                        if (what.empty())
                            continue;

                        boost::ssub_match match = what[1];
                        if (!match.matched)
                            continue;

                        version = boost::trim_copy(string(match.first, match.second));
                        break;
                    }
                }
                
                size_t pos1 = text.find("{{BASH:");
                if (pos1 == string::npos)
                    break;

                pos1 += 7;

                size_t pos2 = text.find("}}", pos1);
                if (pos2 == string::npos)
                    break;

                text = text.substr(pos1, pos2-pos1);

                vector<string> bashTags;
                boost::split(bashTags, text, boost::is_any_of(","));

                for (int i=0,max=bashTags.size(); i<max; ++i) {
                    tags.insert(Tag(bashTags[i]));
                }

                break;
            }
        }
	}

    void Plugin::Merge(const Plugin& plugin, bool ifDisabled) {
        //If 'name' differs or if 'enabled' is false for the given plugin, don't change anything.
        if (!boost::iequals(name, plugin.Name()) || (!plugin.Enabled() && !ifDisabled))
            return;

        //The following should be replaced.
        enabled = plugin.Enabled();
        priority = plugin.Priority();
        if (!plugin.Masters().empty())
            masters = plugin.Masters();
        if (!plugin.FormIDs().empty())
            formIDs = plugin.FormIDs();
        if (!isMaster)
            isMaster = plugin.IsMaster();

        //Merge the following. If any files in the source already exist in the destination, they will be skipped. Files have display strings and condition strings which aren't considered when comparing them, so will be lost if the plugin being merged in has additional data in these strings.
        std::set<File> files = plugin.LoadAfter();
        loadAfter.insert(files.begin(), files.end());

        files = plugin.Reqs();
        requirements.insert(files.begin(), files.end());

        files = plugin.Incs();
        incompatibilities.insert(files.begin(), files.end());

        //Merge Bash Tags too. Conditions are ignored during comparison, but if a tag is added and removed, both instances will be in the set.
        std::set<Tag> bashTags = plugin.Tags();
        tags.insert(bashTags.begin(), bashTags.end());

        //Messages are in an ordered list, and should be fully merged.
        std::list<Message> pMessages = plugin.Messages();
        messages.insert(messages.end(), pMessages.begin(), pMessages.end());
        
        return;
    }
    
    Plugin Plugin::DiffMetadata(const Plugin& plugin) const {
        Plugin p(name);
        p.Priority(priority);

        //Compare this plugin against the given plugin.
        set<File> files = plugin.LoadAfter();
        set<File> filesDiff;
        set_symmetric_difference(files.begin(), files.end(), loadAfter.begin(), loadAfter.end(), inserter(filesDiff, filesDiff.begin()));
        p.LoadAfter(filesDiff);

        filesDiff.clear();
        files = plugin.Reqs();
        set_symmetric_difference(files.begin(), files.end(), requirements.begin(), requirements.end(), inserter(filesDiff, filesDiff.begin()));
        p.Reqs(filesDiff);

        filesDiff.clear();
        files = plugin.Incs();
        set_symmetric_difference(files.begin(), files.end(), incompatibilities.begin(), incompatibilities.end(), inserter(filesDiff, filesDiff.begin()));
        p.Incs(filesDiff);

        list<Message> msgs1 = plugin.Messages();
        list<Message> msgs2 = messages;
        msgs1.sort();
        msgs2.sort();
        list<Message> mDiff;
        set_symmetric_difference(msgs1.begin(), msgs1.end(), msgs2.begin(), msgs2.end(), inserter(mDiff, mDiff.begin()));
        p.Messages(mDiff);

        set<Tag> bashTags = plugin.Tags();
        set<Tag> tagDiff;
        set_symmetric_difference(bashTags.begin(), bashTags.end(), tags.begin(), tags.end(), inserter(tagDiff, tagDiff.begin()));
        p.Tags(tagDiff);

        return p;
    }

    std::string Plugin::Name() const {
        return name;
    }

    bool Plugin::Enabled() const {
        return enabled;
    }

    int Plugin::Priority() const {
        return priority;
    }

    std::set<File> Plugin::LoadAfter() const {
        return loadAfter;
    }

    std::set<File> Plugin::Reqs() const {
        return requirements;
    }

    std::set<File> Plugin::Incs() const {
        return incompatibilities;
    }

    std::list<Message> Plugin::Messages() const {
        return messages;
    }

    std::set<Tag> Plugin::Tags() const {
        return tags;
    }

    void Plugin::Name(const std::string& n) {
        name = n;
    }

    void Plugin::Enabled(const bool e) {
        enabled = e;
    }

    void Plugin::Priority(const int p) {
        priority = p;
    }

    void Plugin::LoadAfter(const std::set<File>& l) {
        loadAfter = l;
    }

    void Plugin::Reqs(const std::set<File>& r) {
        requirements = r;
    }

    void Plugin::Incs(const std::set<File>& i) {
        incompatibilities = i;
    }

    void Plugin::Messages(const std::list<Message>& m) {
        messages = m;
    }

    void Plugin::Tags(const std::set<Tag>& t) {
        tags = t;
    }

    void Plugin::EvalAllConditions(boss::Game& game, const unsigned int language) {
        for (set<File>::iterator it = loadAfter.begin(); it != loadAfter.end();) {
            if (!it->EvalCondition(game))
                loadAfter.erase(it++);
            else
                ++it;
        }

        for (set<File>::iterator it = requirements.begin(); it != requirements.end();) {
            if (!it->EvalCondition(game))
                requirements.erase(it++);
            else
                ++it;
        }

        for (set<File>::iterator it = incompatibilities.begin(); it != incompatibilities.end();) {
            if (!it->EvalCondition(game))
                incompatibilities.erase(it++);
            else
                ++it;
        }

        for (list<Message>::iterator it = messages.begin(); it != messages.end();) {
            if (!it->EvalCondition(game, language))
                it = messages.erase(it);
            else
                ++it;
        }

        for (set<Tag>::iterator it = tags.begin(); it != tags.end();) {
            if (!it->EvalCondition(game))
                tags.erase(it++);
            else
                ++it;
        }
    }

    bool Plugin::HasNameOnly() const {
        return priority == 0 && enabled == true && loadAfter.empty() && requirements.empty() && incompatibilities.empty() && messages.empty() && tags.empty();
    }

    bool Plugin::IsRegexPlugin() const {
        return boost::iends_with(name, "\\.esm") || boost::iends_with(name, "\\.esp");
    }

    bool Plugin::operator == (const Plugin& rhs) const {
        return boost::iequals(name, rhs.Name());
    }

    bool Plugin::operator != (const Plugin& rhs) const {
        return !(*this == rhs);
    }
    
    std::set<FormID> Plugin::FormIDs() const {
		return formIDs;
	}

    std::set<FormID> Plugin::OverlapFormIDs(const Plugin& plugin) const {
        set<FormID> otherFormIDs = plugin.FormIDs();
        set<FormID> overlap;

  /*      for (set<FormID>::iterator it=formIDs.begin(),endit=formIDs.end(); it != endit; ++it) {
            if (otherFormIDs.find(*it) != otherFormIDs.end())
                overlap.insert(*it);
        }

        for (set<FormID>::iterator it=otherFormIDs.begin(),endit=otherFormIDs.end(); it != endit; ++it) {
            if (formIDs.find(*it) != formIDs.end())
                overlap.insert(*it);
        }
*/
        set_intersection(formIDs.begin(), formIDs.end(), otherFormIDs.begin(), otherFormIDs.end(), inserter(overlap, overlap.end()));

        return overlap;
    }
    
	std::vector<std::string> Plugin::Masters() const {
		return masters;
	}
	
	bool Plugin::IsMaster() const {
		return isMaster;
	}

    std::string Plugin::Version() const {
        return version;
    }

    bool Plugin::MustLoadAfter(const Plugin& plugin) const {
        for (vector<string>::const_iterator it=masters.begin(), endIt=masters.end(); it != endIt; ++it) {
            if (boost::iequals(*it, plugin.Name()))
                return true;
        }
        if (find(requirements.begin(), requirements.end(), plugin) != requirements.end())
            return true;
        if (find(loadAfter.begin(), loadAfter.end(), plugin) != loadAfter.end())
            return true;
        return false;
    }

    std::map<string,bool> Plugin::CheckInstallValidity(const Game& game) const {
        map<string,bool> issues;
        if (tags.find(Tag("Filter")) == tags.end()) {
            for (vector<string>::const_iterator it=masters.begin(), endIt=masters.end(); it != endIt; ++it) {
                if (!boost::filesystem::exists(game.DataPath() / *it))
                    issues.insert(pair<string,bool>(*it,false));
            }
        }
        for (set<File>::const_iterator it=requirements.begin(), endIt=requirements.end(); it != endIt; ++it) {
            if (!boost::filesystem::exists(game.DataPath() / it->Name()))
                issues.insert(pair<string,bool>(it->Name(),false));
        }
        for (set<File>::const_iterator it=incompatibilities.begin(), endIt=incompatibilities.end(); it != endIt; ++it) {
            if (boost::filesystem::exists(game.DataPath() / it->Name()))
                issues.insert(pair<string,bool>(it->Name(),true));
        }
        return issues;
    }

    bool operator == (const File& lhs, const Plugin& rhs) {
        return boost::iequals(lhs.Name(), rhs.Name());
    }

    bool alpha_sort(const Plugin& lhs, const Plugin& rhs) {
        return boost::ilexicographical_compare(lhs.Name(), rhs.Name());
    }

    bool load_order_sort(const Plugin& lhs, const Plugin& rhs) {
        if (lhs.IsMaster() && !rhs.IsMaster())
			return true;

        if (!lhs.IsMaster() && rhs.IsMaster())
			return false;

        if (rhs.MustLoadAfter(lhs))
			return true;

        if (lhs.MustLoadAfter(rhs))
			return false;

        if (lhs.Priority() < rhs.Priority())
            return true;
            
        if (lhs.Priority() > rhs.Priority())
            return false;

        if (!lhs.OverlapFormIDs(rhs).empty() && lhs.FormIDs().size() != rhs.FormIDs().size())
            return lhs.FormIDs().size() > rhs.FormIDs().size();

        return boost::ilexicographical_compare(lhs.Name(), rhs.Name());
    }
}
