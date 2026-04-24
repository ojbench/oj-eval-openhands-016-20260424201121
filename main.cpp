#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace std;

const int MAX_KEY_LEN = 64;
const int BLOCK_SIZE = 4096;
const int ORDER = 50; // B+ tree order

struct KeyValue {
    char key[MAX_KEY_LEN + 1];
    int value;
    
    KeyValue() {
        memset(key, 0, sizeof(key));
        value = 0;
    }
    
    KeyValue(const char* k, int v) {
        strncpy(key, k, MAX_KEY_LEN);
        key[MAX_KEY_LEN] = '\0';
        value = v;
    }
    
    bool operator<(const KeyValue& other) const {
        int cmp = strcmp(key, other.key);
        if (cmp != 0) return cmp < 0;
        return value < other.value;
    }
    
    bool operator==(const KeyValue& other) const {
        return strcmp(key, other.key) == 0 && value == other.value;
    }
};

struct Node {
    bool isLeaf;
    int keyCount;
    KeyValue keys[ORDER];
    int children[ORDER + 1]; // file positions for internal nodes
    int next; // for leaf nodes, link to next leaf
    
    Node() {
        isLeaf = true;
        keyCount = 0;
        next = -1;
        for (int i = 0; i <= ORDER; i++) {
            children[i] = -1;
        }
    }
};

class BPlusTree {
private:
    fstream file;
    string filename;
    int rootPos;
    int freePos;
    
    int allocateNode() {
        int pos = freePos;
        freePos += sizeof(Node);
        return pos;
    }
    
    void writeNode(int pos, const Node& node) {
        file.seekp(pos);
        file.write((char*)&node, sizeof(Node));
    }
    
    Node readNode(int pos) {
        Node node;
        file.seekg(pos);
        file.read((char*)&node, sizeof(Node));
        return node;
    }
    
    void writeHeader() {
        file.seekp(0);
        file.write((char*)&rootPos, sizeof(int));
        file.write((char*)&freePos, sizeof(int));
        file.flush();
    }
    
    void readHeader() {
        file.seekg(0);
        file.read((char*)&rootPos, sizeof(int));
        file.read((char*)&freePos, sizeof(int));
    }
    
    void splitChild(int parentPos, int childIndex, int childPos) {
        Node parent = readNode(parentPos);
        Node child = readNode(childPos);
        
        Node newChild;
        newChild.isLeaf = child.isLeaf;
        
        int mid = ORDER / 2;
        
        if (!child.isLeaf) {
            // Internal node split: move middle key up to parent
            newChild.keyCount = ORDER - mid - 1;
            
            for (int i = 0; i < newChild.keyCount; i++) {
                newChild.keys[i] = child.keys[mid + 1 + i];
            }
            
            for (int i = 0; i <= newChild.keyCount; i++) {
                newChild.children[i] = child.children[mid + 1 + i];
            }
            
            child.keyCount = mid;
            
            int newChildPos = allocateNode();
            
            for (int i = parent.keyCount; i > childIndex; i--) {
                parent.children[i + 1] = parent.children[i];
            }
            parent.children[childIndex + 1] = newChildPos;
            
            for (int i = parent.keyCount - 1; i >= childIndex; i--) {
                parent.keys[i + 1] = parent.keys[i];
            }
            parent.keys[childIndex] = child.keys[mid];
            parent.keyCount++;
            
            writeNode(childPos, child);
            writeNode(newChildPos, newChild);
            writeNode(parentPos, parent);
        } else {
            // Leaf node split: copy first key of new child up to parent
            newChild.keyCount = ORDER - mid;
            
            for (int i = 0; i < newChild.keyCount; i++) {
                newChild.keys[i] = child.keys[mid + i];
            }
            
            newChild.next = child.next;
            child.next = freePos;
            child.keyCount = mid;
            
            int newChildPos = allocateNode();
            
            for (int i = parent.keyCount; i > childIndex; i--) {
                parent.children[i + 1] = parent.children[i];
            }
            parent.children[childIndex + 1] = newChildPos;
            
            for (int i = parent.keyCount - 1; i >= childIndex; i--) {
                parent.keys[i + 1] = parent.keys[i];
            }
            parent.keys[childIndex] = newChild.keys[0];
            parent.keyCount++;
            
            writeNode(childPos, child);
            writeNode(newChildPos, newChild);
            writeNode(parentPos, parent);
        }
    }
    
    void insertNonFull(int pos, const KeyValue& kv) {
        Node node = readNode(pos);
        int i = node.keyCount - 1;
        
        if (node.isLeaf) {
            // Check if already exists
            for (int j = 0; j < node.keyCount; j++) {
                if (node.keys[j] == kv) {
                    return; // Already exists, don't insert duplicate
                }
            }
            
            while (i >= 0 && kv < node.keys[i]) {
                node.keys[i + 1] = node.keys[i];
                i--;
            }
            node.keys[i + 1] = kv;
            node.keyCount++;
            writeNode(pos, node);
        } else {
            while (i >= 0 && kv < node.keys[i]) {
                i--;
            }
            i++;
            
            int childPos = node.children[i];
            Node child = readNode(childPos);
            
            if (child.keyCount == ORDER) {
                splitChild(pos, i, childPos);
                node = readNode(pos);
                if (node.keys[i] < kv) {
                    i++;
                }
            }
            insertNonFull(node.children[i], kv);
        }
    }
    
public:
    BPlusTree(const string& fname) : filename(fname) {
        file.open(filename, ios::in | ios::out | ios::binary);
        
        if (!file.is_open()) {
            // Create new file
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            
            rootPos = 2 * sizeof(int);
            freePos = rootPos + sizeof(Node);
            
            Node root;
            root.isLeaf = true;
            root.keyCount = 0;
            
            writeHeader();
            writeNode(rootPos, root);
        } else {
            readHeader();
        }
    }
    
    ~BPlusTree() {
        if (file.is_open()) {
            file.close();
        }
    }
    
    void insert(const char* key, int value) {
        KeyValue kv(key, value);
        
        Node root = readNode(rootPos);
        
        if (root.keyCount == ORDER) {
            Node newRoot;
            newRoot.isLeaf = false;
            newRoot.keyCount = 0;
            newRoot.children[0] = rootPos;
            
            int newRootPos = allocateNode();
            writeNode(newRootPos, newRoot);
            
            splitChild(newRootPos, 0, rootPos);
            rootPos = newRootPos;
            writeHeader();
            
            insertNonFull(rootPos, kv);
        } else {
            insertNonFull(rootPos, kv);
        }
    }
    
    vector<int> find(const char* key) {
        vector<int> result;
        Node node = readNode(rootPos);
        
        while (!node.isLeaf) {
            int i = 0;
            while (i < node.keyCount && strcmp(key, node.keys[i].key) > 0) {
                i++;
            }
            node = readNode(node.children[i]);
        }
        
        // Found leaf node, scan for all matching keys
        while (true) {
            for (int i = 0; i < node.keyCount; i++) {
                if (strcmp(node.keys[i].key, key) == 0) {
                    result.push_back(node.keys[i].value);
                } else if (strcmp(node.keys[i].key, key) > 0) {
                    sort(result.begin(), result.end());
                    return result;
                }
            }
            
            if (node.next == -1) break;
            node = readNode(node.next);
        }
        
        sort(result.begin(), result.end());
        return result;
    }
    
    void remove(const char* key, int value) {
        KeyValue kv(key, value);
        deleteKey(rootPos, kv);
    }
    
private:
    void deleteKey(int pos, const KeyValue& kv) {
        Node node = readNode(pos);
        
        if (node.isLeaf) {
            int i = 0;
            while (i < node.keyCount && !(node.keys[i] == kv)) {
                i++;
            }
            
            if (i < node.keyCount) {
                for (int j = i; j < node.keyCount - 1; j++) {
                    node.keys[j] = node.keys[j + 1];
                }
                node.keyCount--;
                writeNode(pos, node);
            }
        } else {
            int i = 0;
            while (i < node.keyCount && kv < node.keys[i]) {
                i++;
            }
            deleteKey(node.children[i], kv);
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(0);
    
    BPlusTree tree("data.db");
    
    int n;
    cin >> n;
    
    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;
        
        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            tree.insert(key.c_str(), value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            tree.remove(key.c_str(), value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            vector<int> result = tree.find(key.c_str());
            if (result.empty()) {
                cout << "null\n";
            } else {
                for (size_t j = 0; j < result.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << result[j];
                }
                cout << "\n";
            }
        }
    }
    
    return 0;
}
