#include <string.h>
#include <utility>
#include <vector>
#include <map>

struct data_entry
{
    union {
        float time;
        float x;
    };
    union {
        float value;
        float y;
    };
};

struct data_info
{
    int id;     //data id
    int gid;    //group id
    const char * name;
    const char * style;
    std::vector<data_entry> data;
    float range_min;
    float range_max;
};
struct group_info
{
    bool bIsDigital;
    float range_min;
    float range_max;
    std::vector<int> ids;   //data id of this group
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

    std::vector<group_info>                     group;
    std::map<std::string, int>                  gname2ids;      //based on data, provide groupname key based reference
    
    float                                       record_time;
    void ticktock(float time_step_sec) { record_time += time_step_sec; }

    void clear()
    {
        group.clear();
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
        data_entry p;
        p.time = x;
        p.value = value;

        data_info *pdi = NULL;
        group_info *pg = NULL;
        if (name2id.find(data_name) == name2id.end())
        {
            data_info dnew;
            data.push_back(dnew);

            pdi = &(data.back());
            pdi->id = data.size() - 1;
            pdi->name = data_name;
            pdi->style = style;
            pdi->range_min = value;
            pdi->range_max = value;
                        
            //mapping name
            name2id[data_name] = pdi->id;

            //new data name met(rarely), time to maintain groups
            //and add new data in a groups
            char group_name[128];
            get_group_name(data_name, group_name);
            if (gname2ids.find(group_name) == gname2ids.end())
            {
                group_info gnew;
                group.push_back(gnew);
                pg = &(group.back());
                pdi->gid = group.size() - 1;
                gname2ids[group_name] = pdi->gid;

                pg->bIsDigital = true;
                pg->range_max = value;
                pg->range_min = value;
            }
            else
                pdi->gid = gname2ids[group_name];
            
            pg = &(group[pdi->gid]);
            pg->ids.push_back(pdi->id);     //add this new data into group
            if (pg->bIsDigital && (strstr(pdi->style, "d") == NULL)) pg->bIsDigital = false;
        }
        else
        {
            pdi = &(data[name2id[data_name]]);
            pg = &(group[pdi->gid]);
        }
        //maintain data ranges
        if (pdi->range_min > value) pdi->range_min = value;
        if (pdi->range_max < value) pdi->range_max = value;
        if (pg->range_min > value) pg->range_min = value;
        if (pg->range_max < value) pg->range_max = value;
        pdi->data.push_back(p);
    }
};
