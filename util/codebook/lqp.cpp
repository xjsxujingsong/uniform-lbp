//
// Visual Recognition using Local Quantized Patterns
//  Sibt Ul Hussain, Bill Triggs
//

#include <iostream>
#include <cstdio>
#include <string>
#include <fstream>
#include <bitset>
#include <vector>
#include <map>
#include <set>

#include "opencv2/opencv.hpp"

#include "../../texturefeature.h"

using namespace std;
using namespace cv;

#ifdef USE_MEANS
#include "KMajority.hpp"
namespace cv {
void kmajority(InputArray _data, int K,
                   InputOutputArray _bestLabels,
                   TermCriteria criteria, int attempts,
                   int flags, OutputArray _centers = noArray() )
{
    Mat descriptors = _data.getMat();
    // Trivial case: less data than clusters, assign one data point per cluster
    int R = descriptors.rows;
    if (R <= K)
    {
        Mat centroids;
        _bestLabels.create(R,1,CV_32S);
        Mat labels = _bestLabels.getMat();
        for (int i=0; i<R; i++)
        {
            labels.at<int>(i) = i;
            if (_centers.needed())
                centroids.push_back(descriptors.row(i % descriptors.rows));
        }
        if (_centers.needed())
            _centers.assign(centroids);
        return;
    }

    Mat centroids = KMajority::initCentroids(descriptors, K);

    cvflann::Matrix<uchar> inputData(centroids.data, centroids.rows, centroids.cols);
    cvflann::IndexParams params = cvflann::LinearIndexParams();
    cv::Ptr<HammingIndex> index = makePtr<HammingIndex>(inputData, params, HammingDistance());

    // Initially all transactions belong to any cluster
    std::vector<int> belongsTo(descriptors.rows, K);
    // List of distance from each data point to the cluster it belongs to
    //  Initially all transactions are at the farthest possible distance
    //  i.e. m_dim*8 the max Hamming distance
    std::vector<int> distanceTo(descriptors.rows, descriptors.cols * 8);
    // Number of data points assigned to each cluster
    //  Initially no transaction is assigned to any cluster
    std::vector<int> clusterCounts(K, 0);
    KMajority::quantize(index, descriptors, belongsTo, clusterCounts, distanceTo, K);

    for (int iteration = 0; iteration < attempts; ++iteration)
    {
        KMajority::computeCentroids(descriptors, centroids, belongsTo, clusterCounts, distanceTo);

        //index = makePtr<HammingIndex>(inputData, params, HammingDistance()); // is this nessecary ?

        bool converged = KMajority::quantize(index, descriptors, belongsTo, clusterCounts, distanceTo, K);

        KMajority::handleEmptyClusters(belongsTo, clusterCounts, distanceTo, K, descriptors.rows);

        if (converged)
            break;
        cerr << "itr " << iteration << endl;
    }
    Mat(belongsTo).copyTo(_bestLabels);
}
} // cv

void ivec(int i, Mat &m)
{
    Mat r;
    r.push_back(uchar((i>>24) & 0xff));
    r.push_back(uchar((i>>16) & 0xff));
    r.push_back(uchar((i>>8 ) & 0xff));
    r.push_back(uchar((i    ) & 0xff));
    m.push_back(r.reshape(1,1));
}
#endif




struct LQPDisk
{
    int R,P,C,E;

    LQPDisk(int R, int P, int C, int E)
        : R(R),P(P),C(C),E(E)
    {}

    static int* points() {
        static int pts[] = {
             // 1, 0, // Disk1
             // 1, 1,
             // 0, 1,
               2,-1, // Disk2
               2, 0,
               2, 1,
               1, 2,
               0, 2,
              -1, 2,
            //-2, 1, 
            // 3, 0, // Disk3
               3, 1,
               2, 2,
               1, 3,
            // 0, 3,
              -1, 3,
              -2, 2,
              -3, 1,
               4,-2, // Disk4
            // 4,-1,
               4, 0,
            // 4, 1,
               4, 2,
            // 3, 3,
               2, 4,
            // 1, 4,
               0, 4,
            //-1, 4,
              -2, 4,
            //-3, 3,
            //-4, 2,
            //-4, 1 // 13
            // 5, 0, //Disk5
            // 5, 1,
            // 5, 2,
            // 4, 3,
            // 3, 4,
            // 2, 5,
            // 1, 5,
            // 0, 5,
            //-1, 5,
            //-2, 5,
            //-3, 4,
            //-4, 3,
            //-5, 2,
            //-5, 1, // 14
            // 6, 0, //Disk6
            // 6, 1,
            // 6, 2,
            // 5, 3,
            // 4, 4,
            // 3, 5,
            // 2, 6,
            // 1, 6, // 16
            //-1, 6,
            //-2, 6,
            //-3, 5,
            //-4, 4,
            //-5, 3,
            //-6, 2,
            //-6, 1,
            //-6, 0, // 34
        };
        return pts;
    }

    int operator()(const cv::Mat &img, cv::Mat &features) const
    {
        Mat_<uchar> m(img);
        Mat_<int> lbp = Mat_<int>::zeros(img.size());
        int *pts = points();

        for (int i=R; i<img.rows-R; i++)
        {
            for (int j=R; j<img.cols-R; j++)
            {
                unsigned bits=0;
                for (int b=P-1; b>=0; b--)
                {
                    int  v = pts[b*2];
                    int  u = pts[b*2+1];
                    int y1 = i+v;
                    int x1 = j+u;
                    int y2 = i-v;
                    int x2 = j-u;

                    bits |= (m(y1,x1) > m(y2,x2)) << b;
                }
                lbp(i,j) = bits;
            }
        }
        features = lbp;
        return 256;
    }
};


struct MakeLut
{
    map<int,int> occ;

    void add(const Mat &in)
    {
        for (size_t i=0; i<in.total(); i++)
        {
            int id(in.at<int>(i));
            if (occ.find(id)!=occ.end())
                occ[id] ++;
            else occ[id]=1;
        }
    }
    Mat cluster(size_t K, size_t P, size_t E)
    {
        int id=0;
        const int N = 1<<P;
        const int C = occ.size();
        cerr << "N " << N << endl;
        cerr << "C " << C << endl;

        Mat_<int> lut(N, 1);
        #ifdef USE_MEANS
        {
            // try kmeans/kmajority
            Mat labs;
            Mat labels;
            map<int,int>::iterator it = occ.begin();
            for ( ; it != occ.end(); ++it)
            {
                ivec(it->first, labs);
                //labs.push_back(float(it->first));
            }
            //cv::kmeans(labs,K,labels,TermCriteria(),3,KMEANS_RANDOM_CENTERS);
            cv::kmajority(labs,K,labels,TermCriteria(),3,KMEANS_RANDOM_CENTERS);
            cerr << ". " << endl;
            for (int i=0; i<N; i++)
            {
                map<int,int>::iterator it = occ.find(i);
                if (it != occ.end())
                {
                    lut(i) = labels.at<int>(id++);
                }
                else
                {
                    lut(i) = 0;
                }
            }
            cerr << "I " << id << endl;
        }
        #else
        {
            //
            // threshold on occurence count
            //
            // a. make a vector of the occurances and sort that:
            typedef pair<int,int> P;
            vector<P> pairs;
            map<int,int>::iterator it = occ.begin(), del = occ.end();
            for ( ; it != occ.end(); ++it)
            {
                pairs.push_back(make_pair(it->first,it->second));
            }

            struct descending { bool operator () (const P &a, const P&b) { return a.second > b.second; } };
            std::sort(pairs.begin(), pairs.end(), descending());

            // b. procrustes, only most popular items get´an id:
            pairs.resize(K-1); // 0 is our 'noise' bin
            map<int,int> occ2;
            for (vector<P>::iterator it=pairs.begin(); it!=pairs.end(); ++it)
            {
                occ2[it->first] = it->second;
            }
            for (int i=0; i<N; i++)
            {
                map<int,int>::iterator it = occ2.find(i);
                lut(i) = (it != occ2.end()) ? ++id : 0;
            }
            cerr << "I " << id << endl;

            // c. assign empty lut vals to the most prominent cluster
            //    in a E bit hamming neighbourhood:
            for (size_t p=0; p<pairs.size(); ++p)
            {
                int a = pairs[p].first;
                int lp = lut(a);
                for (int i=0; i<N; i++)
                {
                    std::bitset<32> c(a^i);
                    if ((0 == lut(i)) && (c.count() <= E))
                        lut(i) = lp;
                }
                cerr << "P " << p << "\t" << countNonZero(lut) << "\r";
            }
            cerr << "J " << endl;
        }
        return lut;
    }
    #endif
};


void main(int argc, char **argv)
{
    int R = 4;
    int P = 18;
    int C = 220;
    int E = 2;

    if (argc>1) R=atoi(argv[1]);
    if (argc>2) P=atoi(argv[2]);
    if (argc>3) C=atoi(argv[3]);
    if (argc>4) E=atoi(argv[4]);

    LQPDisk sample(R,P,C,E);

    MakeLut resample;
    vector<String> fn,fn2;
    //glob("E:/MEDIA/faces/small/*.jpg", fn);
    glob("E:/code/opencv_p/face3/data/funneled/*.jpg", fn, true);
    //glob("E:/code/opencv_p/face3/data/lfw-deepfunneled/*.jpg", fn, true);
    glob("E:/MEDIA/faces/orl_faces/*.pgm", fn2);
    fn.insert(fn.end(),fn2.begin(), fn2.end());
    cerr << fn.size() << " images." << endl;

    for (size_t i=0; i<fn.size(); i++)
    {
        Mat m = imread(fn[i],0);
        if (m.empty())
        {
            cerr << "no image for " << fn[i] << endl;
            continue;
        }
        resize(m,m,Size(100,100));

        Mat f;
        sample(m,f);

        resample.add(f);
        cerr << i << "\r";
    }
    Mat lut = resample.cluster(sample.C, sample.P, sample.E);
    FileStorage fs("E:/code/opencv_p/face3/data/lqp.xml.gz",FileStorage::WRITE);
    fs << "R" << sample.R;
    fs << "P" << sample.P;
    fs << "C" << sample.C;
    fs << "E" << sample.E;
    fs << "lut" << lut;
    fs << "pts" << Mat_<int>(sample.P*2, 1, sample.points());
    fs.release();
}
