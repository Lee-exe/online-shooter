#include <SFML/Graphics.hpp>
#include <iostream>
#include <time.h>
#include <sqlite3.h>
#include <vector>
#include <cmath>
#include <memory>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <SFML/Network.hpp>
#include <thread>
#include <mutex>
#include <algorithm>
#include <map>

using namespace sf;
using namespace std;
using namespace nlohmann;

const float scale = 2.3;
const int H = 16;
const int W = 16;
const float playerSize = 36*scale/2;
const int TileSize = 40;
const double PI = 3.14159265359;

// OnlineData
std::mutex clientServerDataLock;

struct PlayerServerData
{

    string username;
    float x;
    float y;
    float angle;
    bool shoot;
    float health;
    
    bool operator==(const std::string& name) const
    {
        return username == name;
    }

};

struct sendPowerUp
{
    int id;
    float x;
    float y;
};

struct SpawnPowerUp
{
    int id;
    float x;
    float y;
};

void getDataFromServer(bool &running, bool &enabeld, sf::TcpSocket& socket, PlayerServerData& data, vector<PlayerServerData>& serverData, vector<sendPowerUp>& sendPowerUps, vector<SpawnPowerUp>& spawnPowerUps, bool& keepAlive)
{

    vector<PlayerServerData> temp;
    while (running)
    {
        
        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        sf::Packet packet;

        if (enabeld)
        {
            
            {

            packet << "fromClient_playerData";

            std::lock_guard<std::mutex> lock(clientServerDataLock);
            packet << data.username << data.x << data.y << data.angle << data.shoot << data.health;
            packet << int(sendPowerUps.size());
            // cout << "send PowerUps size: " << int(sendPowerUps.size()) << "\n";
            for (sendPowerUp i:sendPowerUps)
            {
                packet << i.id << i.x << i.y;
            }
            }

            if (socket.send(packet) != sf::Socket::Status::Done) {continue;}

            sf::Packet response;

            if(socket.receive(response) != sf::Socket::Status::Done) {continue;}
            
            // READ PLAYER DATA / Keep Alive
            std::string packetType, username;

            response >> packetType;

            float x,y, angle, health;
            bool shoot;
            int size;
            response >> size;
            {
            std::lock_guard<std::mutex> lock(clientServerDataLock);
            serverData.clear();
            for (int i=0;i<size;i++)
            {
                response >> username >> x >> y >> angle >> shoot >> health;
                PlayerServerData d = {username, x, y, angle, shoot, health};
                serverData.push_back(d);
            }
            
            // READ ITEM DATA
            int id;
            response >> size;
            // cout << "receive PowerUps size: " << size << "\n";
            for (int i=0;i<size;i++)
            {
                response >> id >> x >> y;
                SpawnPowerUp p = {id, x, y};
                spawnPowerUps.push_back(p);
            }

            }
        }
        else
        {
            // cout << "start..";
            packet << "fromClient_keepAlive";
            packet << data.username;
            // cout << "send..";

            if (socket.send(packet) != sf::Socket::Status::Done) {
                {
                std::lock_guard<std::mutex> lock(clientServerDataLock);
                keepAlive = false;
                }
                continue;
            }

            sf::Packet response;
            // cout << "receive..";

            if(socket.receive(response) != sf::Socket::Status::Done) {
                {
                std::lock_guard<std::mutex> lock(clientServerDataLock);
                keepAlive = false;
                }
                continue;
            }

            std::string packetType;
            response >> packetType;
            // cout << "check..";
            if(packetType != "fromServer_keepAlive") {continue;}

            bool state;
            response >> state;
            // cout << state << "\n";
            {
            std::lock_guard<std::mutex> lock(clientServerDataLock);
            keepAlive = state;
            }
        }
    }
}

struct connectToServerTCP_ret
{
    bool suc;
    sf::TcpSocket socket;
};

connectToServerTCP_ret connectToServerTCP()
{

    connectToServerTCP_ret out;
    out.suc = true;

    // Es soll mit dem Packet der Nutzername gesendet werden.

    // std::cout << "connect to server...";
    
    auto resolvedIp = sf::IpAddress::resolve("82.165.71.192");

    if (!resolvedIp)
    {
        std::cout << " failed!\nip failed\n";
        out.suc = false;
        return out;
    }

    sf::IpAddress serverIp = *resolvedIp;

    auto status = out.socket.connect(serverIp, 5000);

    if (status != sf::Socket::Status::Done)
    {
        // std::cout << " failed\nserver not active\n";
        out.suc = false;
        return out;
    }

    // std::cout << " done!\n";
    return out;
}

// LocalGame

struct Pos
{
    float x;
    float y;
};

bool collision(Pos pos, Pos dpos, const float size, vector<bool> &map)
{
    if(dpos.x > 0 or dpos.y > 0)
    {
        if(dpos.x > 0)
        {
            return map.at(int(int((pos.y + dpos.y + size) / (TileSize*scale))*W+int((pos.x + dpos.x + size) / (TileSize*scale)))) or map.at(int(int((pos.y + dpos.y) / (TileSize*scale))*W+int((pos.x + dpos.x + size) / (TileSize*scale))));
        } 
        else 
        {
            return map.at(int(int((pos.y + dpos.y + size) / (TileSize*scale))*W+int((pos.x + dpos.x + size) / (TileSize*scale)))) or map.at(int(int((pos.y + dpos.y + size) / (TileSize*scale))*W+int((pos.x + dpos.x) / (TileSize*scale))));
        }
    }
    else 
    {
        if(dpos.y < 0)
        {
            return map.at(int(int((pos.y + dpos.y) / (TileSize*scale))*W+int((pos.x + dpos.x) / (TileSize*scale)))) or map.at(int(int((pos.y + dpos.y) / (TileSize*scale))*W+int((pos.x + dpos.x + size) / (TileSize*scale))));
        } 
        else 
        {
            return map.at(int(int((pos.y + dpos.y) / (TileSize*scale))*W+int((pos.x + dpos.x) / (TileSize*scale)))) or map.at(int(int((pos.y + dpos.y + size) / (TileSize*scale))*W+int((pos.x + dpos.x) / (TileSize*scale))));
        }
    }
}

float absf(float a)
{
    if (a<0)
    {
        return -a;
    }
    return a;
}

struct LoginData 
{
    string username;
    string password;
};

struct LiveBarColorBorder
{
    struct colorCode
    {
        int r;
        int g;
        int b;
    };
    int min;
    int max;
    colorCode color;
};

class connectDataBase{
    sqlite3* db;
    public:
    void open(const char *filepath){
        sqlite3_open(filepath, &db);
    }
    void close(){
        sqlite3_close(db);
    }
    vector<uint8_t> getMap(int id){

        vector<uint8_t> out;

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db,
            "SELECT map FROM maps WHERE id = ?;",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_int(stmt, 1, id);  // index, id

        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const void* data = sqlite3_column_blob(stmt, 0);
            int size = sqlite3_column_bytes(stmt, 0);

            std::vector<uint8_t> chunk(
                static_cast<const uint8_t*>(data),
                static_cast<const uint8_t*>(data) + size
            );
            out = chunk;
        }

        sqlite3_finalize(stmt);

        return out;
    }
    void saveUserData(string username, string password){

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db,
            "UPDATE userdata SET username = ?, password = ? WHERE id=1;",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

    } 
    LoginData getUserdata(int id){

        LoginData out;

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db,
            "SELECT username, password FROM userdata WHERE id = ?;",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_int(stmt, 1, id);  // index, id

        if (sqlite3_step(stmt) == SQLITE_ROW)
        {

            const void* dataUsername = sqlite3_column_text(stmt, 0);
            int sizeUsername = sqlite3_column_bytes(stmt, 0);

            string username(
                static_cast<const char*>(dataUsername),
                static_cast<const char*>(dataUsername) + sizeUsername
            );

            const void* dataPassword = sqlite3_column_text(stmt, 1);
            int sizePassword = sqlite3_column_bytes(stmt, 1);

            string password(
                static_cast<const char*>(dataPassword),
                static_cast<const char*>(dataPassword) + sizePassword
            );

            out = {username, password};
        }

        sqlite3_finalize(stmt);

        return out;
    }

};

class Player
{
    public:
    Pos pos;
    int health;
    string username;

    void load(Pos _pos, int _health)
    {
        health = _health;
        pos = _pos;
    }
    void respawn(const Pos spawnPos[])
    {
        pos = spawnPos[rand()%4];
    }
};

class Button
{
    public:
    Pos pos;
    Vector2u size;
    bool active = 0;
    bool hiddn = false;

    void load(Pos _pos, Vector2u _size)
    {
        pos = _pos;
        size = _size;
    }
    bool update(Vector2i mousePos)
    {
        if(!hiddn and mousePos.x > pos.x and mousePos.x < pos.x+size.x and mousePos.y > pos.y and mousePos.y < pos.y+size.y){
            active = true;
            if (Mouse::isButtonPressed(sf::Mouse::Button::Left))
            {
                return true;
            }
        } else {
            active = false;
        }
        return false;
    }
};

class PowerUp
{
    public:
    float x,y;
    int id;
    float angle;
    float velo;
    bool left;
    void spawn(int _id, float _x, float _y)
    {
        // id = rand()%3;
        // pos = {_pos.x + (rand()%15-7), _pos.y + (rand()%35-17)};
        // while (collision(pos, {0,0}, 22, _map))
        // {
        //     pos = {_pos.x + (rand()%15-7), _pos.y + (rand()%35-17)};
        // }
        // angle = pos.x - _pos.x;
        // left = pos.x < _pos.x;
        // velo = (7-abs(pos.x - _pos.x))/12.0f+1;

        id = _id;
        x = _x;
        y = _y;
        angle = x - _x;
        velo = (7-abs(x - _x))/12.0f+1;
        left = x < _x;
        if (angle < 0)
        {left = true;}
    }
    void update(vector<bool> &_map)
    {
        if (left)
        {
            angle -= (angle-180)/200.0f;
            if (abs(angle-180) < 5)
            {
                velo /= 1.05f;
            }
        }
        else
        {
            angle -= (angle+180)/200.0f;
            if (abs(angle+180) < 5)
            {
                velo /= 1.05f;
            }
        }
        Pos dpos;
        dpos = {float(velo * sin(angle/180*PI) / 13.0f), 0};
        if (!collision({x,y}, dpos, 22, _map))
        {
            x += dpos.x;
        }
        else
        {
            angle = -angle;
            left = !left;
            //pos.x += angle / absf(angle)*5;
        }
        dpos = {0, float(velo * -cos(angle/180*PI)/ 15.0f)};
        if (!collision({x,y}, dpos, 22, _map))
        {
            y += dpos.y;
        } 
        else 
        {
            if (angle > 90)
            {
                velo = 0;
            } else {
            left = !left;
            // cout << "vor decke: " << angle;
            angle = 180-angle;
            // cout << "... nach: " << angle << "\n";
            }
        }
    }
};

struct Hitbox
{
    Pos pos;
    Pos size;
    PlayerServerData* playerOb;
};

struct damageRet
{
    bool hit;
    PlayerServerData* tar;
};

vector<bool> convertToMap(vector<uint8_t> map)
{
    vector<bool> out, temp;
    bool res;
    for (uint8_t i:map)
    {
        uint8_t bits = i;
        temp.clear();
        for (int k=0;k<8;k++)
        {
            res = bits & 1;
            bits >>= 1;
            temp.push_back(res);
        }
        for (int k=0;k<8;k++)
        {
            out.push_back(temp.back());
            temp.pop_back();
        }
    }
    return out;
}

vector<bool> loadMapfromdb()
{
    vector<bool> map;
    connectDataBase db;
    db.open("map.db");
    map = convertToMap(db.getMap(1));
    db.close();
    return map;
}

class Bullet
{
    public:
    Pos pos;
    double velo;
    float rant;
    string owner;
    Pos size {5*scale, 4*scale};
    void spawn(Pos _pos, float _angle, double _velo, string _username)
    {
        pos = _pos;
        rant = _angle / 180 * PI;
        velo = _velo;
        owner = _username;
    }
    bool update(vector<bool> &map)
    {
        pos.x += cos(rant) * velo;
        pos.y += sin(rant) * velo;
        return collision(pos, {0,0}, 3, map);
    }
    damageRet damage(vector<Hitbox>& hbs)
    {
        for (Hitbox& h:hbs)
        {
            if (
            h.pos.x < pos.x + size.x && h.pos.x + h.size.x > pos.x && h.pos.y < pos.y + size.y && h.pos.y + h.size.y > pos.y
            )
            {
                return {true, h.playerOb};
            }
        }
        return {false, nullptr};
    }
};

bool sichtbar(int x, int y, vector<bool> &map, Pos playerPos)
{
    Vector2f tileCenter {
        x * TileSize * scale + (TileSize * scale) / 2.0f,
        y * TileSize * scale + (TileSize * scale) / 2.0f
    };

    Vector2f playerCenter {
        playerPos.x + playerSize / 2.0f,
        playerPos.y + playerSize / 2.0f
    };

    Vector2f TP {
        playerCenter.x - tileCenter.x,
        playerCenter.y - tileCenter.y
    };

    float distance = sqrt(TP.x * TP.x + TP.y * TP.y);

    int steps = max(1, (int)(distance / (TileSize * scale / 10)));

    for (int i = 1; i < steps; i++)
    {
        Pos checkPos {
            tileCenter.x + TP.x / steps * i,
            tileCenter.y + TP.y / steps * i
        };

        if (collision(checkPos, {0,0}, 1.0, map))
        {
            return false;
        }
    }

    return true;
}

void loadFromTileSet(Sprite* s, Vector2i pos, Vector2i size, Vector2f scale_xy)
{
    s->setTextureRect(IntRect(pos, size));
    s->scale(scale_xy);
}

void drawBTN(Button* ptr, RenderWindow* win, Sprite* inactive, Sprite* active)
{
    if (!ptr->hiddn and ptr->active) 
    {
        active->setPosition({ptr->pos.x, ptr->pos.y}); 
        win->draw(*active);
    } 
    else if (!ptr->hiddn) 
    {
        inactive->setPosition({ptr->pos.x, ptr->pos.y}); 
        win->draw(*inactive);
    }
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    output->append((char*)contents, size * nmemb);
    return size * nmemb;
}

struct LogInRes
{
    bool sucsess;
    string token;
};

LogInRes getlogInData(LoginData ld_) {
    
    CURL* curl = curl_easy_init();
    bool suc {0};
    string token = "";

    if (curl)
    {
        std::string response;

        // url
        curl_easy_setopt(curl, CURLOPT_URL, "https://online-storage.net/logIn");

        // post definiert
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // nutzerdaten
        std::string data = "{\"username\":\"" + ld_.username + "\",\"password\":\"" + ld_.password + "\"}";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        // header
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // responce-speicher definieren
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // post
        CURLcode res = curl_easy_perform(curl);

        suc = res == CURLE_OK;

        if(suc){

            // find state in string
            json responce_data = json::parse(response);

            // std::cout << responce_data["message"] << std::endl;
            suc = responce_data["state"];
            if (suc)
            {token = responce_data["token"];}

        }

        // speicher freigeben
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

    }

    LogInRes out;
    out.sucsess = suc;
    out.token = token;
    
    return out;

}

LogInRes logIn(string username, string password)
{   

    if (username == "" or password == "") {return {false, ""};}

    // post LOGIN
    LogInRes res = getlogInData({username, password});

    if (!res.sucsess) {return res;}

    // Bei erflog nutzerdaten in datenbank speichern

    connectDataBase db;
    db.open("userData.db");
    db.saveUserData(username, password);
    db.close();

    return res;

}

void logOut(string username, string token)
{
CURL* curl = curl_easy_init();
    bool suc {0};

    if (curl)
    {
        std::string response;

        // url
        curl_easy_setopt(curl, CURLOPT_URL, "https://online-storage.net/logOut");

        // post definiert
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        // nutzerdaten
        std::string data = "{\"username\":\"" + username + "\",\"token\":\"" + token + "\"}";

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());

        // header
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // responce-speicher definieren
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // post
        CURLcode res = curl_easy_perform(curl);

        suc = res == CURLE_OK;

        if(suc){
        
            // find state in string
            json responce_data = json::parse(response);

            // std::cout << responce_data["message"] << std::endl;
            suc = responce_data["state"];
        }

        // speicher freigeben
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

    }
}

Pos mapFCords(float x, float y)
{
    return {(float(TileSize)*x+float(TileSize)/2)*scale,(float(TileSize)*y+float(TileSize)/2)*scale};
}

int main()
{
    // random
    srand(time(0));

    // Window
    uint16_t window_width = 900;
    uint16_t window_heigth = 900;
    RenderWindow window(VideoMode({window_width, window_heigth}), "online shooter");
    bool menueOpen = true;

    // Font
    Font font;
    if (!font.openFromFile("arial.ttf")) {
        return -1;
    }

    // Textures & Sprites
    Texture t;
    bool yaaas = t.loadFromFile("images/tileset.png");

    Vector2f TileScale = {scale, scale};
    Vector2f PlayerScale = {scale/2.0f, scale/2.0f};

    Sprite wall(t), fullWall(t), floor(t), darkFloor(t), playerSprite(t), playerShadow(t), gun(t), bullet(t);
    loadFromTileSet(&wall       , {0,0}, {40,40}, TileScale);
    loadFromTileSet(&fullWall   , {0, 40}, {40, 40}, TileScale);
    loadFromTileSet(&floor      , {40, 0}, {40, 40}, TileScale);
    loadFromTileSet(&darkFloor  , {80, 0}, {40, 40}, TileScale);
    loadFromTileSet(&playerSprite, {120, 0}, {40, 40}, PlayerScale);
    loadFromTileSet(&playerShadow, {120, 40}, {40, 40}, PlayerScale);
    loadFromTileSet(&gun        , {160, 0}, {22, 13}, PlayerScale);
    loadFromTileSet(&bullet     , {160, 14}, {5, 4}, TileScale);
    bullet.setOrigin({2.5,2});

    // Buttons
    Vector2f buttonScale = {4.0f,4.0f};
    Sprite loginBTNSurf(t), startBTNSurf(t), quitBTNSurf(t), loginBTNActiveSurf(t), startBTNActiveSurf(t), quitBTNActiveSurf(t), backBTNSurf(t), backBTNActiveSurf(t), trashBTNSurf(t), trashBTNActiveSurf(t), textfield(t), textfieldActive(t), succRES(t), failedRES(t), deathRES(t), serverInactiveRES(t), serverReloadSurf(t), startDisabledSurf(t);
    loadFromTileSet(&loginBTNSurf, {184,  1}, {35, 11}, buttonScale);
    loadFromTileSet(&startBTNSurf, {184, 13}, {35, 11}, buttonScale);
    loadFromTileSet(&quitBTNSurf , {184,  25}, {35, 11}, buttonScale);
    loadFromTileSet(&backBTNSurf , {220,  1}, {35, 11}, buttonScale);
    loadFromTileSet(&loginBTNActiveSurf, {184,  37}, {35, 11}, buttonScale);
    loadFromTileSet(&startBTNActiveSurf, {184, 49}, {35, 11}, buttonScale);
    loadFromTileSet(&quitBTNActiveSurf , {184,  61}, {35, 11}, buttonScale);
    loadFromTileSet(&backBTNActiveSurf , {220,  13}, {35, 11}, buttonScale);
    loadFromTileSet(&trashBTNSurf, {172, 13}, {11, 11}, buttonScale);
    loadFromTileSet(&trashBTNActiveSurf , {172,  13}, {11, 11}, buttonScale);
    loadFromTileSet(&textfield, {40, 40}, {80, 11}, buttonScale);
    loadFromTileSet(&textfieldActive , {40,  51}, {80, 11}, buttonScale);
    loadFromTileSet(&serverReloadSurf, {161,  55}, {9, 8}, {buttonScale.x / 2.0f, buttonScale.y / 2.0f});

    loadFromTileSet(&startDisabledSurf, {220, 49}, {35, 11}, buttonScale);
    startDisabledSurf.setPosition({650,425});

    loadFromTileSet(&succRES, {172, 25}, {11,11}, buttonScale);
    loadFromTileSet(&failedRES, {172, 37}, {11,11}, buttonScale);
    loadFromTileSet(&deathRES, {172, 49}, {11,11}, buttonScale);
    loadFromTileSet(&serverInactiveRES, {172, 61}, {11,11}, buttonScale);
    succRES.setPosition({800,325});
    failedRES.setPosition({800,325});
    deathRES.setPosition({800,325});
    serverInactiveRES.setPosition({800,325});

    // Powerups
    Vector2f powerupsScale = {2.0f,2.0f};
    Sprite speedPUSurf(t), healthPUSurf(t), visionPUSurf(t);
    loadFromTileSet(&speedPUSurf, {160,  19}, {11, 11}, powerupsScale);
    loadFromTileSet(&healthPUSurf, {160, 31}, {11, 11}, powerupsScale);
    loadFromTileSet(&visionPUSurf , {160,  43}, {11, 11}, powerupsScale);

    vector<Sprite*> powerUpsSurfs {
        &speedPUSurf, 
        &healthPUSurf,
        &visionPUSurf 
    };

    // background
    Texture tb;
    bool yaas = tb.loadFromFile("images/background.png");
    Sprite background(tb);
    loadFromTileSet(&background, {0,0}, {220,220}, {window_width/220.f, window_heigth/220.f});

    Button loginBTN;
    Button startBTN;
    Button quitBTN;
    Button backBTN;
    Button trashUNBTN;
    Button textfieldUN;
    Button trashPWBTN;
    Button textfieldPW;
    Button serverReload;

    loginBTN.load({650,325} , {static_cast<uint16_t>(35 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    startBTN.load({650,425}, {static_cast<uint16_t>(35 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    quitBTN.load({650,525} , {static_cast<uint16_t>(35 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    backBTN.load({650,325} , {static_cast<uint16_t>(35 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    trashUNBTN.load({850,410} , {static_cast<uint16_t>(11 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    trashPWBTN.load({850,460} , {static_cast<uint16_t>(11 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    textfieldUN.load({500,410} , {static_cast<uint16_t>(80 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    textfieldPW.load({500,460} , {static_cast<uint16_t>(80 * buttonScale.x),static_cast<uint16_t>(11 * buttonScale.y)});
    serverReload.load({840,300} , {static_cast<uint16_t>(9 * buttonScale.x / 2.0f),static_cast<uint16_t>(8 * buttonScale.y / 2.0f)});

    backBTN.hiddn = true;
    trashUNBTN.hiddn = true;
    trashPWBTN.hiddn = true;
    textfieldUN.hiddn = true;
    textfieldPW.hiddn = true;

    bool mousePressed = false;
    bool loginmeneu = false;
    bool loggedIN = false;
    bool serverActive = false;
    string token = "";

    // Map init
    vector<bool> map = loadMapfromdb();
    const Pos spawnPos[] = {
        mapFCords(1,1),
        mapFCords(14,1),
        mapFCords(2,12),
        mapFCords(13,12)
    };

    // Lebensanzeige
    RectangleShape livebar({32*scale/2.0f,3*scale/2.0f});
    livebar.setFillColor(Color::Black);
    livebar.setOrigin({32*scale/4.0f, 3*scale/4.0f});
    RectangleShape livebarFilling({scale/2.0f, scale/2.0f});
    livebarFilling.setFillColor(Color::Green);

    vector<LiveBarColorBorder> livebarColorBorders = {
        {0,16,  {134,0,0}}, 
        {16,33, {255,0,0}},
        {33,50, {255,174,0}},
        {50,66, {255,217,0}},
        {66,83, {179,255,0}},
        {83,100,{0,255,0}}
    };

    // Player init
    float angle;
    Vector2f cam{0,0};
    Player player;
    player.load({140,140}, 100);
    player.respawn(spawnPos);

    PlayerServerData playerData;
    PlayerServerData playerData_RECENT;

    std::map<string, float> onlinePlayerShoot;

    // Bullets init
    vector<unique_ptr<Bullet>> bullets;

    // Clock init
    Clock clock;
    float unTimer=0, gunTimer=0, timer=0, delay = 0.01, gunDelay = 0.5, sendShootD = 0.2, sendShootTimer=0, keepAliveTimer=0, keepAliveDelta = 0.1;

    // menue vars
    bool textInput = false;
    string passOrUsern = "username";
    String TEXTInputUsername, TEXTInputPassword;
    Text InputUsername(font, "", 25), InputPassword(font, "", 25);
    InputUsername.setPosition({510, 415});
    InputUsername.setFillColor(Color::Black);
    InputPassword.setPosition({510, 465});
    InputPassword.setFillColor(Color::Black);
    Text UsernameCaption(font, "Username", 15), PasswordCaption(font, "Password", 15);
    UsernameCaption.setPosition({510, 392});
    UsernameCaption.setFillColor(Color::White);
    PasswordCaption.setPosition({510, 503});
    PasswordCaption.setFillColor(Color::White);
    Text ResInterafce(font, "", 15);
    ResInterafce.setPosition({640, 300});
    ResInterafce.setFillColor(Color::Black);

    // username TextObject
    Text TEXTUsername_Player(font, "", 15);
    TEXTUsername_Player.setPosition({0, 0});
    TEXTUsername_Player.setFillColor(Color::White);

    // Automatischer Login
    LoginData UsrData;
    connectDataBase db;
    db.open("userData.db");
    UsrData = db.getUserdata(1);
    player.username = UsrData.username;
    db.close();
    LogInRes res = logIn(UsrData.username, UsrData.password);
    loggedIN = res.sucsess;
    token = res.token;
    playerData.username = UsrData.username;

    connectToServerTCP_ret ret = connectToServerTCP();

    serverActive = ret.suc;

    // powerUps
    vector<unique_ptr<PowerUp>> powerups;
    vector<sendPowerUp> sendPowerUps;
    vector<SpawnPowerUp> spawnPowerUps;

    // Hitbox
    vector<Hitbox> hitboxes;

    // thread
    bool thread_active {true}, thread_enabeld{false}, keepAlive{false};
    vector<PlayerServerData> onlinePlayerData;
    vector<PlayerServerData> onlinePlayerData_RECENT;
    // cout << "build thread...";
    thread updateServerData(getDataFromServer, ref(thread_active), ref(thread_enabeld), ref(ret.socket), ref(playerData), ref(onlinePlayerData), ref(sendPowerUps), ref(spawnPowerUps), ref(keepAlive));
    // cout << " done.\n";

    while(window.isOpen())
    {

        // Vars-Update
        float time = clock.getElapsedTime().asSeconds();
        clock.restart();
        timer+=time;
        unTimer += time;
        keepAliveTimer += time;
        Vector2i mouse;
        mouse = Mouse::getPosition(window);
        if (mousePressed and !Mouse::isButtonPressed(Mouse::Button::Left)) {mousePressed = false;}

        // Process events
        while (const std::optional event = window.pollEvent())
        {
            // Close window: exit
            if (event->is<sf::Event::Closed>())
                window.close();
            if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()){

                if (keyPressed->scancode == sf::Keyboard::Scancode::Escape)
                {
                    window.close();
                }
            }
            if (textInput)
            {
                if(passOrUsern == "username"){
                    // Texteingabe abfangen
                    if (auto* textEvent = event->getIf<sf::Event::TextEntered>()) {
                        char32_t code = textEvent->unicode;
                        
                        // Backspace (ASCII 8) und normale lesbare Zeichen filtern
                        if (code == 8 && !TEXTInputUsername.isEmpty()) {
                            TEXTInputUsername.erase(TEXTInputUsername.getSize() - 1); // Letztes Zeichen löschen
                        } else if (code >= 32) {
                            TEXTInputUsername += code; // Zeichen hinzufügen
                        }
                        InputUsername.setString(TEXTInputUsername);
                    }
                }
                if(passOrUsern == "password"){
                    // Texteingabe abfangen
                    if (auto* textEvent = event->getIf<sf::Event::TextEntered>()) {
                        char32_t code = textEvent->unicode;
                        
                        // Backspace (ASCII 8) und normale lesbare Zeichen filtern
                        if (code == 8 && !TEXTInputPassword.isEmpty()) {
                            TEXTInputPassword.erase(TEXTInputPassword.getSize() - 1); // Letztes Zeichen löschen
                        } else if (code >= 32) {
                            TEXTInputPassword += code; // Zeichen hinzufügen
                        }
                        InputPassword.setString(TEXTInputPassword);
                    }
                }
            }
        }

        // MENÜ
        if (menueOpen)
        {
            window.draw(background);

            // Start Button
            if(serverActive and startBTN.update(mouse) and !mousePressed and loggedIN)
            {
                mousePressed = true;


                menueOpen = false;
                thread_enabeld = true;

            }
            // LoginButton
            if (!mousePressed and loginBTN.update(mouse))
            {
                if(loginmeneu)
                {
                    LogInRes res = logIn(TEXTInputUsername, TEXTInputPassword);
                    loggedIN = res.sucsess;
                    token = res.token;
                    player.username = TEXTInputUsername;
                    playerData.username = TEXTInputUsername;
                }
                else
                {
                    mousePressed = true;

                    loginmeneu = true;
                    startBTN.hiddn = true;
                    quitBTN.hiddn = true;

                    backBTN.hiddn = false;
                    trashUNBTN.hiddn = false;
                    trashPWBTN.hiddn = false;
                    textfieldUN.hiddn = false;
                    textfieldPW.hiddn = false;

                    loginBTN.pos = {650,550};
                    succRES.setPosition({800,550});
                    failedRES.setPosition({800,550});
                    serverInactiveRES.setPosition({800,550});
                    deathRES.setPosition({800,550});
                }

            }
            // Quit Button
            if (!mousePressed and quitBTN.update(mouse))
            {
                mousePressed = true;

                window.close();
            }
            // Back Button
            if (!mousePressed and backBTN.update(mouse))
            {
                mousePressed = true;

                loginmeneu = false;
                startBTN.hiddn = false;
                quitBTN.hiddn = false;

                backBTN.hiddn = true;
                trashUNBTN.hiddn = true;
                trashPWBTN.hiddn = true;
                textfieldUN.hiddn = true;
                textfieldPW.hiddn = true;

                loginBTN.pos = {650,325};
                succRES.setPosition({800,325});
                failedRES.setPosition({800,325});
                serverInactiveRES.setPosition({800,325});
                deathRES.setPosition({800,325});

            }
            // Textfield Username
            if (!mousePressed and textfieldUN.update(mouse))
            {
                mousePressed = true;

                if (textInput and passOrUsern == "username")
                {
                    textInput = false;
                }
                else
                {
                    textInput = true;
                    passOrUsern = "username";
                }
            }
            // Textfield Password
            if (!mousePressed and textfieldPW.update(mouse))
            {
                mousePressed = true;

                if (textInput and passOrUsern == "password")
                {
                    textInput = false;
                }
                else
                {
                    textInput = true;
                    passOrUsern = "password";
                }
            }
            // DeleteContent Username
            if (!mousePressed and trashUNBTN.update(mouse))
            {
                mousePressed = true;

                InputUsername.setString("");
                TEXTInputUsername = "";
            }
            // DeleteContent Password
            if (!mousePressed and trashPWBTN.update(mouse))
            {
                mousePressed = true;

                InputPassword.setString("");
                TEXTInputPassword = "";
            }
            // retry connect to server
            if (!mousePressed and loggedIN and !serverActive and !serverActive and serverReload.update(mouse))
            {
                mousePressed = true;

                ret = connectToServerTCP();

                serverActive = ret.suc;
            }

            if (textInput and passOrUsern == "username") {textfieldUN.active = true;}
            if (textInput and passOrUsern == "password") {textfieldPW.active = true;}
            if (!loggedIN) {startBTN.active = false;}

            drawBTN(&loginBTN, &window, &loginBTNSurf, &loginBTNActiveSurf);

            if (serverActive and loggedIN and !loginmeneu)
            {
                drawBTN(&startBTN, &window, &startBTNSurf, &startBTNActiveSurf);
            }
            else if (!loginmeneu)
            {
                window.draw(startDisabledSurf);
            }
            
            drawBTN(&quitBTN, &window, &quitBTNSurf, &quitBTNActiveSurf);
            drawBTN(&backBTN, &window, &backBTNSurf, &backBTNActiveSurf);
            drawBTN(&trashUNBTN, &window, &trashBTNSurf, &trashBTNActiveSurf);
            drawBTN(&trashPWBTN, &window, &trashBTNSurf, &trashBTNActiveSurf);
            drawBTN(&textfieldUN, &window, &textfield, &textfieldActive);
            drawBTN(&textfieldPW, &window, &textfield, &textfieldActive);
            if (loggedIN and !serverActive)
            {
                drawBTN(&serverReload, &window, &serverReloadSurf, &serverReloadSurf);
            }

            // Draw LoginFeedback
            if (serverActive and loggedIN)
            {
                window.draw(succRES);
                ResInterafce.setString("Server Active, logged in");
            }
            if (!serverActive and loggedIN)
            {
                window.draw(serverInactiveRES);
                ResInterafce.setString("Server not Active, logged in");
            }
            if (serverActive and !loggedIN)
            {
                window.draw(failedRES);
                ResInterafce.setString("Server Active, not logged in");
            }
            if (!serverActive and !loggedIN)
            {
                window.draw(deathRES);
                ResInterafce.setString("Server not Active, not logged in");
            }
        
            // Draw Username & Password
            if (loginmeneu)
            {
                window.draw(InputUsername);
                window.draw(InputPassword);
                window.draw(UsernameCaption);
                window.draw(PasswordCaption);
            }
            window.draw(ResInterafce);

            if (loggedIN and keepAliveTimer > keepAliveDelta)
            {
                keepAliveTimer = 0;
                if (!serverActive)
                {
                    ret = connectToServerTCP();
                    serverActive = ret.suc;
                }
                else
                {
                    std::lock_guard<std::mutex> lock(clientServerDataLock);
                    serverActive = keepAlive;
                }
            }

        }

        // GAME
        if (!menueOpen)
        {    
            gunTimer+=time;
            sendShootTimer+=time;
            float angle = atan2((mouse.y-400)-((player.pos.y+playerSize/2)-cam.y-400),(mouse.x-400)-((player.pos.x+playerSize/2)-cam.x-400)) / PI * 180;

            if (timer > delay){
                cam.x -= (cam.x-player.pos.x+400) / 30.0f;
                cam.y -= (cam.y-player.pos.y+400) / 30.0f;
                if (cam.x < 0){cam.x = 0;}
                if (cam.y < 0){cam.y = 0;}
                if (cam.x > TileSize*scale*W-window_width){cam.x = TileSize*scale*W-window_width;}
                if (cam.y > TileSize*scale*H-window_heigth){cam.y = TileSize*scale*H-window_heigth;}
                if (Keyboard::isKeyPressed(Keyboard::Key::Up   ) or Keyboard::isKeyPressed(Keyboard::Key::W   ))    {if(!collision(player.pos, {0,-scale/1.5f}, playerSize, map)){player.pos.y -= scale/1.5f;}}
                if (Keyboard::isKeyPressed(Keyboard::Key::Left ) or Keyboard::isKeyPressed(Keyboard::Key::A ))  {if(!collision(player.pos, {-scale/1.5f,0}, playerSize, map)){player.pos.x -= scale/1.5f;}}
                if (Keyboard::isKeyPressed(Keyboard::Key::Right) or Keyboard::isKeyPressed(Keyboard::Key::D)) {if(!collision(player.pos, {scale/1.5f,0}, playerSize, map)){player.pos.x += scale/1.5f;}}
                if (Keyboard::isKeyPressed(Keyboard::Key::Down ) or Keyboard::isKeyPressed(Keyboard::Key::S ))  {if(!collision(player.pos, {0,scale/1.5f}, playerSize, map)){player.pos.y += scale/1.5f;}}
                if (Keyboard::isKeyPressed(Keyboard::Key::Space) and gunTimer > gunDelay)
                {
                    bullets.push_back(make_unique<Bullet>());
                    bullets.back()->spawn({player.pos.x + playerSize/2.0f, player.pos.y + playerSize/2.0f}, angle + rand()%5-2, 10, player.username);
                    gunTimer = 0;
                    sendShootTimer = 0;
                    // powerups.push_back(make_unique<PowerUp>());
                    // powerups.back()->spawn(player.pos, map);
                }

                for (auto it = bullets.begin(); it != bullets.end(); )
                {
                    if ((*it)->update(map))
                    {
                        it = bullets.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                timer = 0;
            }

            // draw
            window.clear(Color::Black);

            for (int i=0;i<H;i++)
                for (int j=0;j<W;j++)
                {
                    if (!map[i*W+j]){
                        if (sichtbar(j,i, map, player.pos))
                        {
                            floor.setPosition({j*scale*40.0f-cam.x,i*scale*40.0f-cam.y});
                            window.draw(floor);
                        }
                    }

                }

            playerShadow.setPosition({player.pos.x-cam.x+10, player.pos.y-cam.y+10});
            window.draw(playerShadow);
            
            playerSprite.setPosition({player.pos.x-cam.x, player.pos.y-cam.y});
            window.draw(playerSprite);
            for (auto& i:bullets){
                bullet.setPosition({i->pos.x-cam.x, i->pos.y-cam.y});
                bullet.setRotation(radians(i->rant));
                window.draw(bullet);
            }

            gun.setPosition({player.pos.x+playerSize/2-cam.x, player.pos.y+playerSize/2-cam.y});
            gun.setRotation(degrees(angle));
            window.draw(gun);

            // Draw Online Players
            hitboxes.clear();

            for (PlayerServerData& i :onlinePlayerData)
            {
                Hitbox h {{float(i.x), float(i.y)}, {playerSize, playerSize}, &i};
                hitboxes.push_back(h);

                if(i.username == player.username) {continue;}

                if (onlinePlayerShoot.find(i.username) == onlinePlayerShoot.end())
                {
                    onlinePlayerShoot.insert({i.username, unTimer});
                }

                playerSprite.setPosition({float(i.x)-cam.x, float(i.y)-cam.y});
                window.draw(playerSprite);

                playerShadow.setPosition({float(i.x)-cam.x+10, float(i.y)-cam.y+10});
                window.draw(playerShadow);
                
                playerSprite.setPosition({float(i.x)-cam.x, float(i.y)-cam.y});
                window.draw(playerSprite);

                gun.setPosition({float(i.x)+playerSize/2-cam.x, float(i.y)+playerSize/2-cam.y});
                gun.setRotation(degrees(i.angle));
                window.draw(gun);
                
                // Spawn OnlinePlayerBullet
                
                if (i.shoot and unTimer - onlinePlayerShoot.at(i.username) > gunTimer) 
                {
                    i.shoot = false;
                    onlinePlayerShoot.at(i.username) = unTimer;
                    bullets.push_back(make_unique<Bullet>());
                    bullets.back()->spawn({float(i.x) + playerSize/2.0f, float(i.y) + playerSize/2.0f}, i.angle + rand()%5-2, 10, i.username);
                }

                TEXTUsername_Player.setString(i.username);
                TEXTUsername_Player.setPosition({float(i.x)-cam.x+10, float(i.y)-cam.y-20});
                window.draw(TEXTUsername_Player);

                livebar.setPosition({float(i.x)+playerSize/2-cam.x, float(i.y)+playerSize/2-cam.y-9*scale});
                window.draw(livebar);

                for (LiveBarColorBorder& l:livebarColorBorders){
                    if (l.min <= i.health and l.max >= i.health)
                    {
                        Color c(l.color.r, l.color.g, l.color.b);
                        livebarFilling.setFillColor(c);
                    }
                }
                livebarFilling.setPosition({float(i.x)+playerSize/2-cam.x-(15*scale/2.0f), float(i.y)+playerSize/2-cam.y-9*scale});
                livebarFilling.setScale({scale*(i.health/100.0f*26)/2.0f, scale/2.0f});

                window.draw(livebarFilling);

            }

            for (int i=0;i<H;i++)
                for (int j=0;j<W;j++)
                {
                    if (map[i*W+j]){
                        if (( i < H-1 and map[(i+1)*W+j]) or i==H-1)
                        {
                            fullWall.setPosition({j*scale*40.0f-cam.x,i*scale*40.0f-cam.y});
                            window.draw(fullWall);
                        }
                        else
                        {
                            wall.setPosition({j*scale*40.0f-cam.x,i*scale*40.0f-cam.y});
                            window.draw(wall);
                        }
                    } else {
                        if (!sichtbar(j,i, map, player.pos))
                        {
                            darkFloor.setPosition({j*scale*40.0f-cam.x,i*scale*40.0f-cam.y});
                            window.draw(darkFloor);
                        }
                    }

                }

            TEXTUsername_Player.setString(player.username);
            TEXTUsername_Player.setPosition({float(player.pos.x)-cam.x+10, float(player.pos.y)-cam.y-20});
            window.draw(TEXTUsername_Player);

            livebar.setPosition({player.pos.x+playerSize/2-cam.x, player.pos.y+playerSize/2-cam.y-9*scale});
            window.draw(livebar);

            for (LiveBarColorBorder& i:livebarColorBorders){
                if (i.min <= player.health and i.max >= player.health)
                {
                    Color c(i.color.r, i.color.g, i.color.b);
                    livebarFilling.setFillColor(c);
                }
            }
            livebarFilling.setPosition({player.pos.x+playerSize/2-cam.x-(15*scale/2.0f), player.pos.y+playerSize/2-cam.y-9*scale});
            livebarFilling.setScale({scale*(player.health/100.0f*26)/2.0f, scale/2.0f});

            window.draw(livebarFilling);

            for (auto& i:powerups)
            {
                powerUpsSurfs.at(i->id)->setPosition({i->x-cam.x, i->y-cam.y});
                window.draw(*powerUpsSurfs.at(i->id%3));
                i->update(map);
            }

            // Player Data update

            {
            std::lock_guard<std::mutex> lock(clientServerDataLock);
            playerData.angle = angle;
            playerData.x = player.pos.x;
            playerData.y = player.pos.y;
            playerData.health = player.health;
            // Bullets
            playerData.shoot = false;
            if (sendShootTimer < sendShootD)
            {
                playerData.shoot = true;
            }
            // PowerUps
            for (SpawnPowerUp i: spawnPowerUps)
            {
                powerups.push_back(make_unique<PowerUp>());
                powerups.back()->spawn(i.id, i.x, i.y);
            }
            
            }
            for (auto it = bullets.begin(); it != bullets.end(); )
            {
                damageRet ret = (*it)->damage(hitboxes);
                if (ret.hit && ret.tar != nullptr && ret.tar->username != (*it)->owner)
                {
                    // cout << "username from bullet" << (*it)->owner << " - target hit:" << ret.tar->username << "\n";
                    it = bullets.erase(it);
                    // ret.tar->health -= 10;
                    if (ret.tar->username == player.username)
                    {
                        player.health -= 10;
                        if (player.health <= 0)
                        {
                            // sendPowerUps.push_back({rand()*3 + (rand()*rand())%3, player.pos.x + (rand()%15-7), player.pos.y + (rand()%35-17)});
                            player.health = 100; //  ^ ID ^       ^  WICH PU ^
                            player.respawn(spawnPos);
                        }
                    }
                }
                else
                {
                    ++it;
                }
            }
        }

        // Update the window
        window.display();

    }

    // Log Out
    if (loggedIN){logOut(player.username, token);}

    thread_active = false;

    if(updateServerData.joinable())
    {
        updateServerData.join();
    }
    
    return 0;
}
