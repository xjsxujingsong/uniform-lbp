#include "opencv2/core/utility.hpp"
#include "opencv2/xfeatures2d.hpp"
#include "opencv2/opencv.hpp"

#include "texturefeature.h"
#include "util/pcanet/net.h"
#include "landmarks.h"
#if 0
 #include "profile.h"
#endif

#include <vector>
using std::vector;
#include <iostream>
using std::cerr;
using std::endl;


using namespace cv;
using namespace TextureFeature;

namespace TextureFeatureImpl
{

//
// this is the most simple one.
//
struct ExtractorPixels : public TextureFeature::Extractor
{
    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        features = img.reshape(1,1);
        return features.total() * features.elemSize();
    }
};


//
// later use gridded histograms the same way as with lbp(h)
//
struct FeatureGrad
{
    int nsec;
    FeatureGrad(int nsec=45) : nsec(nsec) {}

    int operator() (const Mat &I, Mat &fI) const
    {
        Mat s1, s2, s3(I.size(), CV_32F);
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);
        hal::fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        s3 /= (360/nsec);
        fI = s3;
        //s3.convertTo(fI,CV_8U);
        return (nsec+1); //*2;
    }
};




struct FeatureLbp
{
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                uchar cen = img(r,c);
                v |= (img(r-1,c  ) > cen) << 0;
                v |= (img(r-1,c+1) > cen) << 1;
                v |= (img(r  ,c+1) > cen) << 2;
                v |= (img(r+1,c+1) > cen) << 3;
                v |= (img(r+1,c  ) > cen) << 4;
                v |= (img(r+1,c-1) > cen) << 5;
                v |= (img(r  ,c-1) > cen) << 6;
                v |= (img(r-1,c-1) > cen) << 7;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 256;
    }
};

//
// "Description of Interest Regions with Center-Symmetric Local Binary Patterns"
// (http://www.ee.oulu.fi/mvg/files/pdf/pdf_750.pdf).
//    (w/o threshold)
//
struct FeatureCsLbp
{
    int radius;
    FeatureCsLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c  ) > img(r+R,c  )) << 0;
                v |= (img(r-R,c+R) > img(r+R,c-R)) << 1;
                v |= (img(r  ,c+R) > img(r  ,c-R)) << 2;
                v |= (img(r+R,c+R) > img(r-R,c-R)) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};


//
// / \
// \ /
//
struct FeatureDiamondLbp
{
    int radius;
    FeatureDiamondLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c  ) > img(r  ,c+R)) << 0;
                v |= (img(r  ,c+R) > img(r+R,c  )) << 1;
                v |= (img(r+R,c  ) > img(r  ,c-R)) << 2;
                v |= (img(r  ,c-R) > img(r-R,c  )) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};


//  _ _
// |   |
// |_ _|
//
struct FeatureSquareLbp
{
    int radius;
    FeatureSquareLbp(int r=1) : radius(r) {}
    int operator() (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int R=radius;
        for (int r=R; r<img.rows-R; r++)
        {
            for (int c=R; c<img.cols-R; c++)
            {
                uchar v = 0;
                v |= (img(r-R,c-R) > img(r-R,c+R)) << 0;
                v |= (img(r-R,c+R) > img(r+R,c+R)) << 1;
                v |= (img(r+R,c+R) > img(r+R,c-R)) << 2;
                v |= (img(r+R,c-R) > img(r-R,c-R)) << 3;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 16;
    }
};

//
// Antonio Fernandez, Marcos X. Alvarez, Francesco Bianconi:
// "Texture description through histograms of equivalent patterns"
//
//    basically, this is just 1/2 of the full lbp-circle (4bits / 16 bins only!)
//
struct FeatureMTS
{
    int operator () (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> img(I);
        Mat_<uchar> fea(I.size(), 0);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                uchar cen = img(r,c);
                v |= (img(r-1,c  ) > cen) << 0;
                v |= (img(r-1,c+1) > cen) << 1;
                v |= (img(r  ,c+1) > cen) << 2;
                v |= (img(r+1,c+1) > cen) << 3;
                fea(r,c) = v;
            }
        }
        fI = fea;
        return 16;
    }
};


//
// just run around in a circle (instead of comparing to the center) ..
//
struct FeatureBGC1
{
    int operator () (const Mat &I, Mat &fI) const
    {
        Mat_<uchar> feature(I.size(),0);
        Mat_<uchar> img(I);
        const int m=1;
        for (int r=m; r<img.rows-m; r++)
        {
            for (int c=m; c<img.cols-m; c++)
            {
                uchar v = 0;
                v |= (img(r-1,c  ) > img(r-1,c-1)) << 0;
                v |= (img(r-1,c+1) > img(r-1,c  )) << 1;
                v |= (img(r  ,c+1) > img(r-1,c+1)) << 2;
                v |= (img(r+1,c+1) > img(r  ,c+1)) << 3;
                v |= (img(r+1,c  ) > img(r+1,c+1)) << 4;
                v |= (img(r+1,c-1) > img(r+1,c  )) << 5;
                v |= (img(r  ,c-1) > img(r+1,c-1)) << 6;
                v |= (img(r-1,c-1) > img(r  ,c-1)) << 7;
                feature(r,c) = v;
            }
        }
        fI = feature;
        return 256;
    }
};



//
// Wolf, Hassner, Taigman : "Descriptor Based Methods in the Wild"
// 3.1 Three-Patch LBP Codes
//
struct FeatureTPLbp
{
    int operator () (const Mat &img, Mat &features) const
    {
        Mat_<uchar> I(img);
        Mat_<uchar> fI(I.size(), 0);
        const int R=2;
        for (int r=R; r<I.rows-R; r++)
        {
            for (int c=R; c<I.cols-R; c++)
            {
                uchar v = 0;
                v |= ((I(r,c) - I(r  ,c-2)) > (I(r,c) - I(r-2,c  ))) * 1;
                v |= ((I(r,c) - I(r-1,c-1)) > (I(r,c) - I(r-1,c+1))) * 2;
                v |= ((I(r,c) - I(r-2,c  )) > (I(r,c) - I(r  ,c+2))) * 4;
                v |= ((I(r,c) - I(r-1,c+1)) > (I(r,c) - I(r+1,c+1))) * 8;
                v |= ((I(r,c) - I(r  ,c+2)) > (I(r,c) - I(r+1,c  ))) * 16;
                v |= ((I(r,c) - I(r+1,c+1)) > (I(r,c) - I(r+1,c-1))) * 32;
                v |= ((I(r,c) - I(r+1,c  )) > (I(r,c) - I(r  ,c-2))) * 64;
                v |= ((I(r,c) - I(r+1,c-1)) > (I(r,c) - I(r-1,c-1))) * 128;
                fI(r,c) = v;
            }
        }
        features = fI;
        return 256;
    }
};



//
// Wolf, Hassner, Taigman : "Descriptor Based Methods in the Wild"
// 3.2 Four-Patch LBP Codes (4bits / 16bins only !)
//
struct FeatureFPLbp
{
    int radius;
    FeatureFPLbp(int r=2) : radius(r) {}

    int operator () (const Mat &img, Mat &features) const
    {
        Mat_<uchar> I(img);
        Mat_<uchar> fI(I.size(), 0);
        const int R=radius;
        for (int r=R; r<I.rows-R; r++)
        {
            for (int c=R; c<I.cols-R; c++)
            {
                uchar v = 0;
                v |= ((I(r  ,c+1) - I(r+R,c+R)) > (I(r  ,c-1) - I(r-R,c-R))) * 1;
                v |= ((I(r+1,c+1) - I(r+R,c  )) > (I(r-1,c-1) - I(r-R,c  ))) * 2;
                v |= ((I(r+1,c  ) - I(r+R,c-R)) > (I(r-1,c  ) - I(r-R,c+R))) * 4;
                v |= ((I(r+1,c-1) - I(r  ,c-R)) > (I(r-1,c+1) - I(r  ,c+R))) * 8;
                fI(r,c) = v;
            }
        }
        features = fI;
        return 16;
    }
};






//
// LTPTransform stolen from https://github.com/biometrics/openbr
//
struct FeatureLTP
{
    unsigned short lut[8][3];
    int radius;
    float thresholdPos;
    float thresholdNeg;

    FeatureLTP()
        : radius(1)
        , thresholdPos( 0.1f)
        , thresholdNeg(-0.1f)

    {
        unsigned short cnt = 0;
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 3; j++)
                lut[i][j] = cnt++;
            cnt++;  //we skip the 4th number (only three patterns)
        }
    }

    int operator() (const Mat &I, Mat &fI) const
    {
        CV_Assert(I.isContinuous() && (I.channels() == 1));

        Mat_<float>  m(I); 
        Mat_<ushort> n(m.size());
        n = 0;

        const float *p = (const float*)m.ptr();
        for (int r=radius; r<m.rows-radius; r++)
        {
            for (int c=radius; c<m.cols-radius; c++)
            {
                const float cval = p[r * m.cols + c];
                static const int off[8][2] = {-1,-1, -1,0, -1,1, 0,1, 1,1, 1,0, 1,-1, 0,-1};
                for (int li=0; li<8; li++)
                {   // walk neighbours:
                    int y = r + off[li][0] * radius;
                    int x = c + off[li][1] * radius;
                    float diff = p[y * m.cols + x] - cval;
                    n(r,c) += (diff > thresholdPos) ? lut[li][0] :
                              (diff < thresholdNeg) ? lut[li][1] : lut[li][2];
                }
            }
        }
        fI = n;
        return 256;
    }
};


//
// Visual Recognition using Local Quantized Patterns
//  Sibt Ul Hussain, Bill Triggs
//
struct LQPDisk
{
    int R;
    int P;
    int C;

    Mat_<int> lut;
    Mat_<int> pts;

    LQPDisk()
    {
        // pretrained from util/codebook/lqp.cpp
        FileStorage fs("data/lqp.xml.gz",FileStorage::READ);
        int E;
        fs["R"] >> R;
        fs["P"] >> P;
        fs["C"] >> C;
        fs["E"] >> E;
        fs["lut"] >> lut;
        fs["pts"] >> pts;
        cerr << "LQPDisk R "<< R << " P " << P <<" C " << C << " E " << E << endl;
    }

    int operator()(const cv::Mat &img, cv::Mat &features) const
    {       
        Mat_<uchar> m(img);
        Mat_<uchar> lbp = Mat_<uchar>::zeros(img.size());
        for (int i=R; i<img.rows-R; i++)
        {
            for (int j=R; j<img.cols-R; j++)
            {
                unsigned bits=0;
                for (int b=P-1; b>=0; b--)
                {
                    int v  = pts(b*2);
                    int u  = pts(b*2+1);
                    int y1 = i+v;
                    int x1 = j+u;
                    int y2 = i-v;
                    int x2 = j-u;

                    bits |= (m(y1,x1) > m(y2,x2)) << b;
                }
                lbp(i,j) = lut(bits);
            }
        }
        features = lbp;
        return C;
    }
};





static void hist_patch(const Mat_<uchar> &fI, Mat &histo, int histSize=256)
{
    Mat_<float> h(1, histSize, 0.0f);
    for (int i=0; i<fI.rows; i++)
    {
        for (int j=0; j<fI.cols; j++)
        {
            int v = int(fI(i,j));
            h( v ) += 1.0f;
        }
    }
    histo.push_back(h.reshape(1,1));
}


//
// uniform 8bit lookup
//
static void hist_patch_uniform(const Mat_<uchar> &fI, Mat &histo)
{
    static int uniform[256] =
    {   // the well known original uniform2 pattern
        0,1,2,3,4,58,5,6,7,58,58,58,8,58,9,10,11,58,58,58,58,58,58,58,12,58,58,58,13,58,
        14,15,16,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,17,58,58,58,58,58,58,58,18,
        58,58,58,19,58,20,21,22,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,58,58,58,58,58,58,58,58,58,58,58,23,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,58,24,58,58,58,58,58,58,58,25,58,58,58,26,58,27,28,29,30,58,31,58,58,58,32,58,
        58,58,58,58,58,58,33,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,34,58,58,58,58,
        58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,58,
        58,35,36,37,58,38,58,58,58,39,58,58,58,58,58,58,58,40,58,58,58,58,58,58,58,58,58,
        58,58,58,58,58,58,41,42,43,58,44,58,58,58,45,58,58,58,58,58,58,58,46,47,48,58,49,
        58,58,58,50,51,52,58,53,54,55,56,57
    };

    Mat_<float> h(1, 60, 0.0f); // mod4
    for (int i=0; i<fI.rows; i++)
    {
        for (int j=0; j<fI.cols; j++)
        {
            int v = int(fI(i,j));
            h( uniform[v] ) += 1.0f;
        }
    }
    histo.push_back(h.reshape(1,1));
}


//
// concatenate histograms from grid based patches
//
struct GriddedHist
{
    bool uniform;
    int GRIDX,GRIDY;

    GriddedHist(bool uniform=false, int gridx=8, int gridy=8)
        : uniform(uniform)
        , GRIDX(gridx)
        , GRIDY(gridy)
    {}

    void hist(const Mat &feature, Mat &histo, int histSize=256) const
    {
        histo.release();
        int sw = feature.cols/GRIDX;
        int sh = feature.rows/GRIDY;
        for (int i=0; i<GRIDX; i++)
        {
            for (int j=0; j<GRIDY; j++)
            {
                Mat patch(feature, Range(j*sh,(j+1)*sh), Range(i*sw,(i+1)*sw));
                if (uniform && histSize==256)
                    hist_patch_uniform(patch, histo);
                else
                    hist_patch(patch, histo, histSize);
            }
        }
        normalize(histo.reshape(1,1),histo);
    }
};


//
// overlapped pyramid of histogram patches
//  (not resizing the feature/image)
//
struct PyramidGrid
{
    bool uniform;

    PyramidGrid(bool uniform=false): uniform(uniform) {}

    void hist_level(const Mat &feature, Mat &histo, int GRIDX, int GRIDY, int histSize=256) const
    {
        int sw = feature.cols/GRIDX;
        int sh = feature.rows/GRIDY;
        for (int i=0; i<GRIDX; i++)
        {
            for (int j=0; j<GRIDY; j++)
            {
                Mat patch(feature, Range(j*sh,(j+1)*sh), Range(i*sw,(i+1)*sw));
                if (uniform && histSize==256)
                    hist_patch_uniform(patch, histo);
                else
                    hist_patch(patch, histo, histSize);
            }
        }
    }

    void hist(const Mat &feature, Mat &histo, int histSize=256) const
    {
        histo.release();
        int levels[] = {5,6,7,8};
        for (int i=0; i<4; i++)
        {
            hist_level(feature,histo,levels[i],levels[i],histSize);
        }
        normalize(histo.reshape(1,1),histo);
    }
};





//
//
// layered base for lbph,
//  * calc features on the whole image,
//  * calculate the hist on a set of rectangles
//    (which could come from a grid, or a Rects, or a keypoint based model).
//
template <typename Feature, typename Grid>
struct GenericExtractor : public TextureFeature::Extractor
{
    Feature ext;
    Grid grid;

    GenericExtractor(const Feature &ext, const Grid &grid)
        : ext(ext)
        , grid(grid)
    {}

    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat fI;
        int histSize = ext(img, fI);
        grid.hist(fI, features, histSize);
        return features.total() * features.elemSize();
    }
};


//
// instead of adding more bits, concatenate several histograms,
// cslbp + dialbp + sqlbp = 3*16 bins = 12288 feature-bytes.
//
template <typename Grid>
struct CombinedExtractor : public TextureFeature::Extractor
{
    Grid grid;

    CombinedExtractor(const Grid &grid)
        : grid(grid)
    {}

    template <class Extract>
    void extract(const Mat &img, Mat &features, int r) const
    {
        Extract ext(r);
        Mat f,fI;
        int histSize = ext(img, f);
        grid.hist(f, fI, histSize);
        features.push_back(fI.reshape(1,1));
    }
    // TextureFeature::Extractor
    virtual int extract(const Mat &img, Mat &features) const
    {
        extract<FeatureCsLbp>(img,features,2);
        extract<FeatureCsLbp>(img,features,4);
        extract<FeatureFPLbp>(img,features,2);
        extract<FeatureFPLbp>(img,features,4);
        extract<FeatureDiamondLbp>(img,features,3);
        extract<FeatureSquareLbp>(img,features,4);
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



template <typename Grid>
struct GradMagExtractor : public TextureFeature::Extractor
{
    Grid grid;
    int nbins;

    GradMagExtractor(const Grid &grid)
        : grid(grid)
        , nbins(45)
    {}

    // TextureFeature::Extractor
    virtual int extract(const Mat &I, Mat &features) const
    {
        Mat fgrad, fmag;
        Mat s1, s2, s3(I.size(), CV_32F), s4(I.size(), CV_32F);
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);

        hal::fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        fgrad = s3 / (360/nbins);
        fgrad.convertTo(fgrad,CV_8U);
        Mat fg;
        grid.hist(fgrad,fg,nbins+1);
        features.push_back(fg.reshape(1,1));

        hal::magnitude(s1.ptr<float>(0), s2.ptr<float>(0), s4.ptr<float>(0), I.total());
        normalize(s4,fmag);
        fmag.convertTo(fmag,CV_8U,nbins);
        Mat fm;
        grid.hist(fmag,fm,nbins+1);
        features.push_back(fm.reshape(1,1));

        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};

//
// 2d histogram with "rings" of magnitude and "sectors" of gradients.
//
struct ExtractorGradBin : public TextureFeature::Extractor
{
    int nsec,nrad,grid;
    ExtractorGradBin(int nsec=8, int nrad=2, int grid=18) : nsec(nsec), nrad(nrad), grid(grid) {}

    virtual int extract(const Mat &I, Mat &features) const
    {
        Mat s1, s2, s3(I.size(), CV_32F), s4(I.size(), CV_32F), s5;
        Sobel(I, s1, CV_32F, 1, 0);
        Sobel(I, s2, CV_32F, 0, 1);
        hal::fastAtan2(s1.ptr<float>(0), s2.ptr<float>(0), s3.ptr<float>(0), I.total(), true);
        s3 /= (360/nsec);

        hal::magnitude(s1.ptr<float>(0), s2.ptr<float>(0), s4.ptr<float>(0), I.total());
        normalize(s4, s4, nrad, 0, NORM_MINMAX);

        int sx = I.cols/(grid-2);
        int sy = I.rows/(grid-2);
        int nbins = nsec*nrad;
        features = Mat(1,nbins*grid*grid,CV_32F,Scalar(0));
        for (int i=0; i<I.rows; i++)
        {
            int oy = i/sy;
            for (int j=0; j<I.cols; j++)
            {
                int ox = j/sx;
                int off = nbins*(oy*grid + ox);
                int g = (int)s3.at<float>(i,j);
                int m = (int)s4.at<float>(i,j);
                features.at<float>(off + g + m*nsec) ++;
            }
        }
        return features.total() * features.elemSize();
    }
};


struct ExtractorGaborGradBin : public ExtractorGradBin
{
    Size kernel_size;

    ExtractorGaborGradBin(int nsec=8, int nrad=2, int grid=12, int kernel_siz=9)
        : ExtractorGradBin(nsec, nrad, grid)
        , kernel_size(kernel_siz, kernel_siz)
    {}

    void gabor(const Mat &src_f, Mat &features,double sigma, double theta, double lambda, double gamma, double psi) const
    {
        Mat dest,dest8u,his;
        cv::filter2D(src_f, dest, CV_32F, getGaborKernel(kernel_size, sigma,theta, lambda, gamma, psi, CV_64F));
        //dest.convertTo(dest8u, CV_8U);
        ExtractorGradBin::extract(dest, his);
        features.push_back(his.reshape(1, 1));
    }

    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat src_f;
        img.convertTo(src_f, CV_32F, 1.0/255.0);
        gabor(src_f, features, 8,4,90,15,0);
        gabor(src_f, features, 8,4,45,30,1);
        gabor(src_f, features, 8,4,45,45,0);
        gabor(src_f, features, 8,4,90,60,1);
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};

//
//template < class Descriptor >
//struct ExtractorGridFeature2d : public TextureFeature::Extractor
//{
//    int grid;
//
//    ExtractorGridFeature2d(int g=10) : grid(g) {}
//
//    virtual int extract(const Mat &img, Mat &features) const
//    {
//        float gw = float(img.cols) / grid;
//        float gh = float(img.rows) / grid;
//        vector<KeyPoint> kp;
//        for (float i=gh/2; i<img.rows-gh; i+=gh)
//        {
//            for (float j=gw/2; j<img.cols-gw; j+=gw)
//            {
//                KeyPoint k(j, i, gh);
//                kp.push_back(k);
//            }
//        }
//        Ptr<Feature2D> f2d = Descriptor::create();
//        f2d->compute(img, kp, features);
//
//        features = features.reshape(1,1);
//        return features.total() * features.elemSize();
//    }
//};
//typedef ExtractorGridFeature2d<ORB> ExtractorORBGrid;
//typedef ExtractorGridFeature2d<BRISK> ExtractorBRISKGrid;
//typedef ExtractorGridFeature2d<xfeatures2d::FREAK> ExtractorFREAKGrid;
//typedef ExtractorGridFeature2d<xfeatures2d::SIFT> ExtractorSIFTGrid;
//typedef ExtractorGridFeature2d<xfeatures2d::BriefDescriptorExtractor> ExtractorBRIEFGrid;

struct Patcher : public TextureFeature::Extractor
{
    Ptr<Landmarks> land;
    int patchSize;

    Patcher(int size=20) 
        : land(createLandmarks()) 
        , patchSize(size)
    {}

    virtual int extract(const Mat &img, Mat &features) const
    {
        vector<Point> kp;
        land->extract(img,kp);
        Mat f;
        for (size_t k=0; k<kp.size(); k++)
        {
            Mat patch;
            getRectSubPix(img, Size(patchSize,patchSize), kp[k], patch);
            f.push_back(patch.reshape(1,1));
        }
        features = f.reshape(1,1);
        return features.total() * features.elemSize();
    }
};


//
// "Review and Implementation of High-Dimensional Local Binary Patterns
//    and Its Application to Face Recognition"
//    Bor-Chun Chen, Chu-Song Chen, Winston Hsu
//
struct HighDimLbp : public TextureFeature::Extractor
{
    FeatureFPLbp lbp;
    Ptr<Landmarks> land;
    HighDimLbp() : land(createLandmarks()) {}
    virtual int extract(const Mat &img, Mat &features) const
    {
        int gr=10; // 10 used in paper
        vector<Point> kp;
        land->extract(img,kp);

        Mat histo;
        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
        float offsets_16[] = {
            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
        };
        int noff = 16;
        float *off = offsets_16;
        for (int i=0; i<5; i++)
        {
            float s = scale[i];
            Mat f1,f2,imgs;
            resize(img,imgs,Size(),s,s);
            int histSize = lbp(imgs,f1);

            for (size_t k=0; k<kp.size(); k++)
            {
                Point2f pt(kp[k]);
                for (int o=0; o<noff; o++)
                {
                    Mat patch;
                    getRectSubPix(f1, Size(gr,gr), Point2f(pt.x*s + off[o*2]*gr, pt.y*s + off[o*2+1]*gr), patch);
                    hist_patch(patch, histo, histSize);
                }
            }
        }
        normalize(histo.reshape(1,1), features);
        return features.total() * features.elemSize();
    }
};

struct HighDimLbpPCA : public TextureFeature::Extractor
{
    Ptr<Landmarks> land;
    FeatureFPLbp lbp;
    //FeatureLbp lbp;
    PCA pca[20];

    HighDimLbpPCA()
        : land(createLandmarks())
    {
        FileStorage fs("data/fplbp_pca.xml.gz",FileStorage::READ);
        CV_Assert(fs.isOpened());
        FileNode pnodes = fs["hdlbp"];
        int i=0;
        for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
        {
            pca[i++].read(*it);
        }
        fs.release();
    }

    virtual int extract(const Mat &img, Mat &features) const
    {
        int gr=10; // 10 used in paper
        vector<Point> kp;
        land->extract(img,kp);
        CV_Assert(kp.size()==20);
        Mat histo;
        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
        float offsets_16[] = {
            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
        };
        int noff = 16;
        float *off = offsets_16;
        Mat h[20];
        for (int i=0; i<5; i++)
        {
            float s = scale[i];
            Mat f1,f2,imgs;
            resize(img,imgs,Size(),s,s);
            int histSize = lbp(imgs,f1);

            for (size_t k=0; k<kp.size(); k++)
            {
                Point2f pt(kp[k]);
                for (int o=0; o<noff; o++)
                {
                    Mat patch;
                    getRectSubPix(f1, Size(gr,gr), Point2f(pt.x*s + off[o*2]*gr, pt.y*s + off[o*2+1]*gr), patch);
                    hist_patch(patch, h[k], histSize);
                    //hist_patch_uniform(patch, h[k], histSize);
                }
            }
        }
        for (size_t k=0; k<kp.size(); k++)
        {
            Mat hx = h[k].reshape(1,1);
            normalize(hx,hx);
            Mat hy = pca[k].project(hx);
            histo.push_back(hy);
        }
        normalize(histo.reshape(1,1), features);
        return features.total() * features.elemSize();
    }
};

//
//struct HighDimPCASift : public TextureFeature::Extractor
//{
//    Ptr<Landmarks> land;
//    Ptr<Feature2D> sift;
//    PCA pca[20];
//
//    HighDimPCASift()
//        : sift(xfeatures2d::SIFT::create())
//        , land(createLandmarks())
//    {
//        FileStorage fs("data/hd_pcasift_20.xml.gz",FileStorage::READ);
//        CV_Assert(fs.isOpened());
//        FileNode pnodes = fs["hd_pcasift"];
//        int i=0;
//        for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
//        {
//            pca[i++].read(*it);
//        }
//        fs.release();
//    }
//    virtual int extract(const Mat &img, Mat &features) const
//    {
//        int gr=5; // 10 used in paper
//        vector<Point> pt;
//        land->extract(img,pt);
//        CV_Assert(pt.size()==20);
//
//        Mat histo;
//        float scale[] = {0.75f, 1.06f, 1.5f, 2.2f, 3.0f}; // http://bcsiriuschen.github.io/High-Dimensional-LBP/
//        float offsets_16[] = {
//            -1.5f,-1.5f, -0.5f,-1.5f, 0.5f,-1.5f, 1.5f,-1.5f,
//            -1.5f,-0.5f, -0.5f,-0.5f, 0.5f,-0.5f, 1.5f,-0.5f,
//            -1.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.5f, 0.5f,
//            -1.5f, 1.5f, -0.5f, 1.5f, 0.5f, 1.5f, 1.5f, 1.5f
//        };
//        float offsets_9[] = {
//            -1.f,-1.f, 0.f,-1.f, 1.f,-1.f,
//            -1.f, 0.f, 0.f, 0.f, 1.f, 0.f,
//            -1.f, 1.f, 0.f, 1.f, 1.f, 1.f,
//        };
//        int noff = 16;
//        float *off = offsets_16;
////        for (int i=0; i<5; i++)
//        {
//            float s = 1.0f;//scale[i];
//            Mat f1,f2,imgs;
//            resize(img,imgs,Size(),s,s);
//
//            for (size_t k=0; k<pt.size(); k++)
//            {
//                vector<KeyPoint> kp;
//                Mat h;
//                for (int o=0; o<noff; o++)
//                {
//                    kp.push_back(KeyPoint(pt[k].x*s + off[o*2]*gr, pt[k].y*s + off[o*2+1]*gr, float(gr)));
//                }
//                sift->compute(imgs,kp,h);
//                for (size_t j=0; j<kp.size(); j++)
//                {
//                    Mat hx = h.row(j).t();
//                    hx.push_back(float(kp[j].pt.x/img.cols - 0.5));
//                    hx.push_back(float(kp[j].pt.y/img.rows - 0.5));
//                    Mat hy = pca[k].project(hx.reshape(1,1));
//                    histo.push_back(hy);
//                }
//            }
//        }
//        features = histo.reshape(1,1);
//        return features.total() * features.elemSize();
//    }
//};
//


struct HighDimGrad : public TextureFeature::Extractor
{
    Ptr<Landmarks> land;
    ExtractorGradBin grad;

    HighDimGrad()
        : grad(18,2,8)
        , land(createLandmarks())
    {
    }
    virtual int extract(const Mat &img, Mat &features) const
    {
        vector<Point> pt;
        land->extract(img, pt);
        CV_Assert(pt.size() == 20);

        Mat histo;
        for (size_t k=0; k<pt.size(); k++)
        {
            Mat h;
            Mat patch;
            getRectSubPix(img, Size(32,32), pt[k], patch);
            grad.extract(patch, h);
            histo.push_back(h.reshape(1,1));
        }
        features = histo.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



//
// CDIKP: A Highly-Compact Local Feature Descriptor
//        Yun-Ta Tsai, Quan Wang, Suya You
//
struct ExtractorCDIKP : public TextureFeature::Extractor
{
    Ptr<Landmarks> land;

    ExtractorCDIKP() : land(createLandmarks()) {}

    template<class T>
    void fast_had(int ndim, int lev, T *in, T *out) const
    {
        int h = lev/2;
        for (int j=0; j<ndim/lev; j++)
        {
            for(int i=0; i<h; i++)
            {
                out[i]   = in[i] + in[i+h];
                out[i+h] = in[i] - in[i+h];
            }
            out += lev;
            in  += lev;
        }
    }
    Mat project(Mat &in) const
    {
        const int keep = 10;
        Mat h = in.clone().reshape(1,1);
        Mat wh(1, h.total(), h.type());
        for (int lev=in.total(); lev>2; lev/=2)
        {
            fast_had(in.total(), lev, h.ptr<float>(), wh.ptr<float>());
            if (lev>4) cv::swap(h,wh);
        }
        return wh(Rect(0,0,keep,1));
    }
    virtual int extract(const Mat &img, Mat &features) const
    {
        Mat fI;
        img.convertTo(fI,CV_32F);
        Mat dx,dy;
        Sobel(fI,dx,CV_32F,1,0);
        Sobel(fI,dy,CV_32F,0,1);
        const int ps = 16;
        const float step = 3;
        for (float i=ps/4; i<img.rows-3*ps/4; i+=step)
        {
            for (float j=ps/4; j<img.cols-3*ps/4; j+=step)
            {
                Mat patch;
                cv::getRectSubPix(dx,Size(ps,ps),Point2f(j,i),patch);
                features.push_back(project(patch));
                cv::getRectSubPix(dy,Size(ps,ps),Point2f(j,i),patch);
                features.push_back(project(patch));
            }
        }
        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }
};



struct ExtractorPNet : public TextureFeature::Extractor
{
    Ptr<PNet> pnet;
    ExtractorPNet(const cv::String &path) : pnet(loadNet(path)) {}

    virtual int extract(const Mat &I, Mat &features) const
    {
        features = pnet->extract(I);
        return features.total() * features.elemSize();
    }
};


//
// Gil Levi and Tal Hassner, "LATCH: Learned Arrangements of Three Patch Codes", arXiv preprint arXiv:1501.03719, 15 Jan. 2015
//
// slightly modified version, that
// uses n locally trained latches
//
struct ExtractorLatch2 : public TextureFeature::Extractor
{
    Ptr<Landmarks> land;
    vector< Mat_<int> > latches;
    int feature_bytes;
    int half_ssd_size;
    int patch_size;

    ExtractorLatch2()
        : land(createLandmarks())
    {
        feature_bytes = 96;
        half_ssd_size = 5;
        patch_size    = 12;
        load("data/latch.xml.gz");
    }

    static bool calculateSums(int count, const Point &pt, const Mat_<int> &points, const Mat &grayImage, int half_ssd_size)
    {
        int ax = points(count)     + (int)(pt.x + 0.5);
        int ay = points(count + 1) + (int)(pt.y + 0.5);
        int bx = points(count + 2) + (int)(pt.x + 0.5);
        int by = points(count + 3) + (int)(pt.y + 0.5);
        int cx = points(count + 4) + (int)(pt.x + 0.5);
        int cy = points(count + 5) + (int)(pt.y + 0.5);
        int suma = 0, sumc = 0;
        int K = half_ssd_size;
        for (int iy = -K; iy <= K; iy++)
        {
            const uchar * Mi_a = grayImage.ptr<uchar>(ay + iy);
            const uchar * Mi_b = grayImage.ptr<uchar>(by + iy);
            const uchar * Mi_c = grayImage.ptr<uchar>(cy + iy);
            for (int ix = -K; ix <= K; ix++)
            {
                double difa = Mi_a[ax + ix] - Mi_b[bx + ix];
                suma += (int)((difa)*(difa));
                double difc = Mi_c[cx + ix] - Mi_b[bx + ix];
                sumc += (int)((difc)*(difc));
            }
        }
        return (suma < sumc);
    }

    void pixelTests(int i, int N, const Point &pt, const Mat &grayImage, Mat &descriptors, int half_ssd_size) const
    {
        int count = 0;
        const Mat_<int> &points = latches[i];
        uchar* desc = descriptors.ptr(i);
        for (int ix=0; ix<N; ix++)
        {
            desc[ix] = 0;
            for (int j=7; j>=0; j--)
            {
                bool bit = calculateSums(count, pt, points, grayImage, half_ssd_size);
                desc[ix] += (uchar)(bit << j);
                count += 6;
            }
        }
    }

    virtual int extract(const Mat &image, Mat &features) const
    {
        Mat blurImage;
           GaussianBlur(image, blurImage, cv::Size(3, 3), 2, 2);
        features.create((int)latches.size(), feature_bytes, CV_8U);

        vector<Point> pts;
        land->extract(image, pts);
        for (size_t i=0; i<latches.size(); i++)
        {
            pixelTests(i, feature_bytes, pts[i], blurImage, features, half_ssd_size);
        }

        features = features.reshape(1,1);
        return features.total() * features.elemSize();
    }

    void load(const String &fn)
    {
        FileStorage fs(fn, FileStorage::READ);
        fs["patch"] >> patch_size;
        fs["bytes"] >> feature_bytes;
        fs["ssd"]   >> half_ssd_size;
        FileNode pnodes = fs["latches"];
        for (FileNodeIterator it=pnodes.begin(); it!=pnodes.end(); ++it)
        {
            Mat_<int> points;
            (*it)["pt"] >> points;
            latches.push_back(points);
        }
        fs.release();
    }
};


//struct ExtractorDaisy : public TextureFeature::Extractor
//{
//    Ptr<xfeatures2d::DAISY> daisy;
//    ExtractorDaisy() : daisy(xfeatures2d::DAISY::create()) {}
//
//    virtual int extract(const Mat &img, Mat &features) const
//    {
//        int step = 9; // dense grid of ~10x10 kp.
//        int patch_size=15;
//        vector<KeyPoint> kps;
//        for (int i=patch_size; i<img.rows-patch_size; i+=step)
//        {
//            for (int j=patch_size; j<img.cols-patch_size; j+=step)
//            {
//                kps.push_back(KeyPoint(float(j), float(i), 1));
//            }
//        }
//        daisy->compute(img,kps,features);
//
//        features = features.reshape(1,1);
//        return features.total() * features.elemSize();
//    }
//};


} // TextureFeatureImpl

//extern Ptr<TextureFeature::Extractor> createFisherVector(const String &fn);
//extern Ptr<TextureFeature::Extractor> createRBMExtractor(const String &fn);

namespace TextureFeature
{
using namespace TextureFeatureImpl;

cv::Ptr<Extractor> createExtractor(int extract)
{
    switch(int(extract))
    {
        case EXT_Pixels:   return makePtr< ExtractorPixels >(); break;
        case EXT_Ltp:      return makePtr< GenericExtractor<FeatureLTP,GriddedHist> >(FeatureLTP(), GriddedHist()); break;
        case EXT_Lbp:      return makePtr< GenericExtractor<FeatureLbp,GriddedHist> >(FeatureLbp(), GriddedHist()); break;
        case EXT_LBP_P:    return makePtr< GenericExtractor<FeatureLbp,PyramidGrid> >(FeatureLbp(), PyramidGrid()); break;
        case EXT_LBPU:     return makePtr< GenericExtractor<FeatureLbp,GriddedHist> >(FeatureLbp(), GriddedHist(true)); break;
        case EXT_LBPU_P:   return makePtr< GenericExtractor<FeatureLbp,PyramidGrid> >(FeatureLbp(), PyramidGrid(true)); break;
        case EXT_LQP:      return makePtr< GenericExtractor<LQPDisk,GriddedHist> >(LQPDisk(), GriddedHist()); break;
        case EXT_TPLbp:    return makePtr< GenericExtractor<FeatureTPLbp,GriddedHist> >(FeatureTPLbp(), GriddedHist()); break;
        case EXT_TPLBP_P:  return makePtr< GenericExtractor<FeatureTPLbp,PyramidGrid> >(FeatureTPLbp(), PyramidGrid()); break;
        case EXT_FPLbp:    return makePtr< GenericExtractor<FeatureFPLbp,GriddedHist> >(FeatureFPLbp(), GriddedHist()); break;
        case EXT_FPLBP_P:  return makePtr< GenericExtractor<FeatureFPLbp,PyramidGrid> >(FeatureFPLbp(), PyramidGrid()); break;
        case EXT_MTS:      return makePtr< GenericExtractor<FeatureMTS,GriddedHist> >(FeatureMTS(), GriddedHist()); break;
        case EXT_MTS_P:    return makePtr< GenericExtractor<FeatureMTS,PyramidGrid> >(FeatureMTS(), PyramidGrid()); break;
        case EXT_BGC1:     return makePtr< GenericExtractor<FeatureBGC1,GriddedHist> >(FeatureBGC1(), GriddedHist()); break;
        case EXT_BGC1_P:   return makePtr< GenericExtractor<FeatureBGC1,PyramidGrid> >(FeatureBGC1(), PyramidGrid()); break;
        case EXT_COMB:     return makePtr< CombinedExtractor<GriddedHist> >(GriddedHist()); break;
        case EXT_COMB_P:   return makePtr< CombinedExtractor<PyramidGrid> >(PyramidGrid()); break;
        //case EXT_Sift:     return makePtr< ExtractorSIFTGrid >(32); break;
        case EXT_Grad:     return makePtr< GenericExtractor<FeatureGrad,GriddedHist> >(FeatureGrad(),GriddedHist());  break;
        case EXT_Grad_P:   return makePtr< GenericExtractor<FeatureGrad,PyramidGrid> >(FeatureGrad(),PyramidGrid()); break;
        case EXT_GradMag:  return makePtr< GradMagExtractor<GriddedHist> >(GriddedHist()); break;
        case EXT_GradMag_P:return makePtr< GradMagExtractor<PyramidGrid> >(PyramidGrid()); break;
        case EXT_GradBin:  return makePtr< ExtractorGradBin >(); break;
        case EXT_GaborGB:  return makePtr< ExtractorGaborGradBin >(); break;
        case EXT_HDGRAD:   return makePtr< HighDimGrad >();  break;
        case EXT_HDLBP:    return makePtr< HighDimLbp >();  break;
        case EXT_HDLBP_PCA:return makePtr< HighDimLbpPCA >();  break;
        //case EXT_PCASIFT:  return makePtr< HighDimPCASift >();  break;
        case EXT_PNET:     return makePtr< ExtractorPNet >("data/pnet.xml");  break;
        case EXT_CDIKP:    return makePtr< ExtractorCDIKP >();  break;
        case EXT_LATCH2:   return makePtr< ExtractorLatch2 >();  break;
        //case EXT_DAISY:    return makePtr< ExtractorDaisy >();  break;
        case EXT_PATCH:    return makePtr< Patcher >();  break;
        //case EXT_RBM:      return createRBMExtractor("data/rbm.xml.gz");  break;
        default: cerr << "extraction " << extract << " is not yet supported." << endl; exit(-1);
    }
    return Ptr<Extractor>();
}

} // namespace TextureFeatureImpl
