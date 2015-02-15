#include <utility>
#include <vector>
#include <map>

struct data_info
{
    int id;
    const char * name;
    const char * style;
    std::vector<std::pair<float, float> > data;
    float range_min;
    float range_max;
};
static void get_group_name(const char * name, char * gname)
{
    const char * pdot = strrchr(name, '.');
    int cnt = strlen(name);
    if (pdot)
        cnt = pdot - name;

    strncpy(gname, name, cnt);
    gname[cnt] = '\0';
}
//it is very danger to use pointer with STL, so we use vector & id reference as main storage mechanism
struct DataManager
{
    std::vector<data_info>                      data;           //for most basic storage & iteration (both refernced by id).
    std::map<std::string, int>                  name2id;        //based on data, provide dataname key based reference
    std::map<std::string, std::vector<int>>     gname2ids;      //based on data, provide groupname key based reference


    float                                       record_time;
    void ticktock(float time_step_sec) { record_time += time_step_sec; }

    void clear()
    {
        data.clear();
        name2id.clear();
        gname2ids.clear();
        record_time = 0;
    }
    
    void record(const char * data_name, const char * style, float value)
    {
        record(data_name, value, record_time, style);
    }

    void record(const char * data_name, float value, float x, const char * style)
    {
        std::pair<float, float> p(x, value);
        if (name2id.find(data_name) == name2id.end())
        {
            data_info di;
            di.id = data.size();
            di.name = data_name;
            di.style = style;
            di.data.push_back(p);
            di.range_min = value;
            di.range_max = value;
            data.push_back(di);

            //mapping name
            name2id[data_name] = di.id;

            //new data name met(rarely), time to maintain groups
            char group_name[128];
            get_group_name(data_name, group_name);
            if (gname2ids.find(group_name) == gname2ids.end())
            {
                std::vector<int> ids;
                gname2ids[group_name] = ids;
            }
            gname2ids[group_name].push_back(di.id); //new data in a groups
        }
        else
        {
            data_info &di = data[name2id[data_name]];
            //maintain data ranges
            if (di.range_min > value) di.range_min = value;
            if (di.range_max < value) di.range_max = value;
            di.data.push_back(p);
        }
    }
};