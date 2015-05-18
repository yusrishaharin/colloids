"""Phase behaviour of Colloid+Polymer mixture according to generalized free volume theory
see Gerard J Fleer and Remco Tuinier, Advances in Colloid and Interface Science 143, 1-47 (2008).
"""

import numpy as np
from scipy.optimize import fsolve, fminbound
from scipy import interpolate
from scipy.misc import derivative
from scipy.integrate import quad

def logOnePlusX(x):
    """Calculate log(1 + x), preventing loss of precision for small values of x."""
    #assert x > -1.0, "Invalid input argument (%f); must be greater than -1.0"%x
    #if np.fabs(x) > 0.375:
        # x is sufficiently large that the obvious evaluation is OK
        #return np.log(1.0 + x);
    # For smaller arguments we use a rational approximation
    # to the function log(1+x) to avoid the loss of precision
    # that would occur if we simply added 1 to x then took the log.
    p1 =  -0.129418923021993e+01
    p2 =   0.405303492862024e+00
    p3 =  -0.178874546012214e-01
    q1 =  -0.162752256355323e+01
    q2 =   0.747811014037616e+00
    q3 =  -0.845104217945565e-01
    t = x/(x + 2.0)
    t2 = t*t
    w = (((p3*t2 + p2)*t2 + p1)*t2 + 1.0)/(((q3*t2 + q2)*t2 + q1)*t2 + 1.0);
    return np.where(np.fabs(x) > 0.375, np.log(1.0 + x), 2.0*t*w);

#conversions from experimental variables to theoretically convinient ones
qR2q = lambda qR: 0.9*qR**0.9
piv2y = lambda piv, qR: qR**3*piv
y2piv = lambda y, qR: y/qR**3
f2vf = lambda f: f/(1+f)
vf2f = lambda vf: vf/(1-vf)

#volume fraction at close packing
eta_cp = np.pi/6*np.sqrt(2)
f_cp = vf2f(eta_cp)

#volume fraction at random close packing
eta_rcp = 0.637
f_rcp = vf2f(eta_rcp)

#Widom insersion theorem gives alpha: the fraction of the volume that is free to the polymers
A = lambda q: (1+q)**3 -1
B = lambda q: 3*q**2*(q+1.5)
C = lambda q: q**3
Q = lambda f, q: A(q)*f + B(q)*f**2 + C(q)*f**3
Q1 = lambda f, q: A(q) + 2*B(q)*f + 3*C(q)*f**2
Q2 = lambda f, q: 2*B(q) + 6*C(q)*f
Q3 = lambda f, q: 6*C(q)
beta = lambda f,q: np.exp(-Q(f,q))
beta1 = lambda f,q: -beta(f,q)*Q1(f,q)
beta2 = lambda f,q: -beta(f,q)*Q2(f,q) - beta1(f,q)*Q1(f,q)
beta3 = lambda f,q: -beta(f,q)*Q3(f,q) - 2*beta1(f,q)*Q2(f,q)- beta2(f,q)*Q1(f,q)
g = lambda f,q: (beta(f,q) - (1+f)*beta1(f,q))/(1+q)**3
h = lambda f,q: beta(f,q) - f*beta1(f,q)
alpha = lambda f, q: beta(f,q)/(1+f)



class EquationOfState:
    """An equation of state for the system without polymer."""
    
    def Z(self, f):
        """Compressibility function of f=phi/(1-phi)"""
        raise NotImplementedError()
    
    def pv_0(self, f):
        """Pressure * volume = phi*Z(phi) function of f=phi/(1-phi)"""
        return f/(1.+f) * self.Z(f)
    
    def pv_0_1(self, f):
        """First derivative of pv_0 with respect to f"""
        return derivative(self.pv_0, f, dx=1e-6)
        
    def pv_0_2(self, f):
        """Second derivative of pv_0 with respect to f"""
        return derivative(self.pv_0, f, dx=1e-3, n=2,order=5)
    
    def tointegrate(self,f):
        """The content of the integral that gives part of the chemical potential"""
        return (self.Z(f)-1)/(1.+f)/f
    
    def mu_0_nolog(self, f):
        """Chemical potential - np.log(f)"""
        return - logOnePlusX(f) + self.Z(f)-1 + np.vectorize(
            lambda y: quad(self.tointegrate, 0, y)[0]
            )(f)
        
    def mu_0(self, f):
        """Chemical potential"""
        return np.log(f) + self.mu_0_nolog(f)
        
    def pv(self, f, piv, q):
        """Pressure * volume for the system with polymers"""
        return self.pv_0(f) + piv * h(f,q)
        
    def mu(self, f, piv, q):
        """Chemical potential for the system with polymers"""
        return self.mu_0(f) + piv * (1+q)**3 * g(f, q)
        
    def mu_of_log(self, F, piv, q):
        """Redefine the chemical potential with F=log(f) as variable. Prevents numerical errors when doing exp(log(f))""" 
        f = np.exp(F)
        return F + self.mu_0_nolog(f)  + piv * (1+q)**3 * g(f, q)
        
    def mu_of_U(self, U, piv, q):
        """Redefine the chemical potential with U=log(u) as variable, with u = 1/f - 1/f_cp. Prevents going over f_cp and numerical errors when doing exp(log(u))"""
        return self.mu_of_log(-np.log(np.exp(U) + 1/f_cp), piv, q)

    def pv_of_log(self, F, piv, q):
        """Redefine pv with F=log(f) as variable. Prevents numerical errors when doing exp(log(f))"""
        return self.pv(np.exp(F), piv, q)
        
    def pv_of_U(self, U, piv, q):
        """Redefine pv with U=log(u) as variable, with u = 1/f - 1/f_cp. Prevents going over f_cp and numerical errors when doing exp(log(u))"""
        return self.pv(1/(np.exp(U) + 1/f_cp), piv, q)
        
    def critical_point(self, q):
        """Critical point coordinates in the (f,PIv) plane function of the effective size ratio q=delta/a"""
        fc = fsolve(lambda f: 1/f + beta3(f,q)/beta2(f,q) - self.pv_0_2(f)/self.pv_0_1(f), 0.5)[0]
        PIvc = self.pv_0_1(fc)/fc/beta2(fc, q)
        return (fc, PIvc)
        
    def binodalGL(self, piv, q, guess=None):
        """return (log(f_gas), log(f_liquid)) on the binodal line at a given insersion work piv"""
        if guess is None:
            fc = self.critical_point(q)[0]
            fspG = fminbound(lambda f: -self.pv(f, piv,q), 0, fc)
            fspL = fminbound(self.pv, fc, 2*fc, args=(piv, q))
            guess = np.log([0.5*fspG, fspL+fspG])
        return fsolve(lambda Fs: [
            self.pv_of_log(Fs[0], piv, q) - self.pv_of_log(Fs[1], piv, q), 
            self.mu_of_log(Fs[0], piv, q) - self.mu_of_log(Fs[1], piv, q)
            ], guess)

    def spinodalGL(self, q, pivs=None):
        """return (piv, f_gas, f_liquid) on the spinodal line at insersion works pivs"""
        fc, pivc = self.critical_point(q)
        if pivs is None:
            pivs = 1/np.linspace(1/pivc, 1/(8*pivc))
        return np.column_stack((pivs, np.vstack([(
            fminbound(lambda f: -self.pv(f, piv, q), 0, fc),
            fminbound(lambda f: self.pv(f, piv, q), fc, 12)
            ) for piv in pivs])))
    
        
    def all_GL(self, q, maxpiv=None):
        """return (piv, f_binodal_gas, f_binodal_liquid, f_spinodal_gas, f_spinodal_liquid) at insersion works piv sampled between the critical point and maxpiv (default to 2.2*critical pressure)"""
        fc, pivc = self.critical_point(q)
        Fc = np.log(fc)
        #start sensibly above the critical point
        startp = pivc*1.1
        fm = fminbound(self.mu, fc, 2*fc, args=(startp, q))
        fM = fminbound(lambda f: -self.pv(f, startp, q), 0, fc)
        initial_guess = np.log([0.5*fM, fm+fM])
        #construct the top of the GL binodal
        if maxpiv is None:
            maxpiv = startp*2
        topp = 1./np.linspace(1./startp, 1./maxpiv)
        topGL = [initial_guess]
        for piv in topp:
            topGL.append(self.binodalGL(piv, q, topGL[-1]))
        #construct the GL binodal between the starting piv and the critical point
        botp = np.linspace(startp, pivc)[:-1]
        botGL = [initial_guess]
        for piv in botp:
            botGL.append(self.binodalGL(piv, q, botGL[-1]))
        #join the two results and convert back from log
        binodal = np.vstack((
            [[pivc, fc, fc]],
            np.column_stack((botp, np.exp(botGL[1:])))[::-1],
            np.column_stack((topp, np.exp(topGL[1:])))[1:]
            ))
        #spinodal at the same pivs
        spinodal = self.spinodalGL(q, binodal[:,0])
        #join everything
        return np.column_stack((binodal, spinodal[:,1:]))
        
class CarnahanStarling(EquationOfState):
    """Carnahan and Starling equation of state for a hard sphere fluid."""
    
    def Z(self, f):
        """Compressibility function of f=phi/(1-phi)"""
        return 2*f**3 + 6*f**2 + 4*f +1
    
    def pv_0(self, f):
        """Pressure * volume = phi*Z(phi) function of f=phi/(1-phi)"""
        return f/(1+f) + 4*f**2 + 2* f**3
    
    def pv_0_1(self, f):
        """First derivative of pv_0 with respect to f"""
        return (1+f)**(-2) + 8*f + 6*f**2
        
    def pv_0_2(self, f):
        """Second derivative of pv_0 with respect to f"""
        return -2*(1+f)**(-3) + 8 + 12*f
        
    def mu_0_nolog(self, f):
        """Chemical potential - np.log(f)"""
        return - logOnePlusX(f) + 8*f + 7*f**2 + 2*f**3
        
class CarnahanStarling2(EquationOfState):
    """Carnahan and Starling equation of state for a hard sphere fluid."""
    
    def Z(self, f):
        """Compressibility function of f=phi/(1-phi)"""
        return 2*f**3 + 6*f**2 + 4*f +1
        
    def tointegrate(self,f):
        """The content of the integral that gives part of the chemical potential"""
        return (2*f**2 + 6*f + 4)/(1.+f)
        
class Kolafa(EquationOfState):
    """Kolafa equation of state for a hard sphere fluid."""
    
    def pv_0(self, f):
        """Pressure * volume = phi*Z(phi) function of f=phi/(1-phi)"""
        return (3 + 15*f + 30*f**2 + 25*f**3 + 5*f**4)*f/(1+f)**2/3.
    
    def pv_0_1(self, f):
        """First derivative of pv_0 with respect to f"""
        return 5*f**2 + 10*f + 4*(1+f)**(-2) - 4/3. * (1+f)**(-3) - 5/3.
        
    def pv_0_2(self, f):
        """Second derivative of pv_0 with respect to f"""
        return 10*(1+f) -8*(1+f)**(-3) +4*(1+f)**(-4)
        
    def mu_0_nolog(self, f):
        """Chemical potential - np.log(f)"""
        return (-8*logOnePlusX(f) + 5 - 4/(f+1) + 25*f + 45*f**2/2. + 5*f**3) / 3.
        
class LeFevre(EquationOfState):
    """Le Fevre equation of state for a hard sphere fluid.
    
    Add a divergence at RCP but is less accurate than CS at low densities, very bad between 0.45 and 0.58"""
    
    def __init__(self):
        self.P = np.poly1d([-0.0972383, -1.21581, -3.89085, -5.35009, -3.40466, -0.826856, 0])
        self.Q = np.poly1d([1, -2.50397, 2.38135, -1.35199, -0.0972383, -0.924177, -0.826856])
    
    def pv_0(self, f):
        """Pressure * volume = phi*Z(phi) function of f=phi/(1-phi)"""
        return self.P(f)/self.Q(f)
    
    def pv_0_1(self, f):
        """First derivative of pv_0 with respect to f"""
        return (self.P.deriv() * self.Q - self.P * self.Q.deriv())(f) / (self.Q**2)(f)
        
    def pv_0_2(self, f):
        """Second derivative of pv_0 with respect to f"""
        return (self.P.deriv(2) * self.Q**2 - 2*self.P.deriv()*self.Q.deriv()*self.Q +2*self.P * self.Q.deriv()**2 - self.P * self.Q * self.Q.deriv(2))(f) / (self.Q**3)(f)
        
    def mu_0_nolog(self, f):
        """Chemical potential - np.log(f)"""
        phi = f2vf(f)
        Q1 = np.poly1d([-1, 0.636566])(phi)
        Q2 = np.poly1d([1, 0.977327])(phi)
        Q3 = np.poly1d([1, -0.646459, 0.468619])(phi)
        Q4 = np.poly1d([1, -1.15801, 0.391873])(phi)
        Z = 1.11967/Q1 +0.0258742/Q2 + (0.342478*phi-0.278592)/Q3 + (0.751322*phi-0.0748109)/Q4
        intZ = -1.75889*np.log(Q1) -0.0264744*np.log(Q2) +0.297249*np.log(Q3) +0.0954345*np.log(Q4) -2.6928*np.arctan(2.43318-4.20234*phi) -0.249103*np.arctan(0.535643-1.65716*phi)
        return - logOnePlusX(f) + Z - 1 + intZ + 2.8219814517647936
        

        
class Hall(EquationOfState):
    """Hall equation of state for a hard sphere crystal."""
    
    def pv_0(self, f):
        """Pressure * volume = phi*Z(phi) function of f=phi/(1-phi)"""
        return 3/(1.0/f - 1/f_cp)
        
    def mu_0(self, f):
        """Chemical potential"""
        u = 1/f - 1/f_cp
        return 2.1306 + 3.0*(1+f)/u/f - 3*np.log(u)
        
    def mu_of_U(self, U, piv, q):
        """Redefine the chemical potential with U=log(u) as variable, with u = 1/f - 1/f_cp. Prevents going over f_cp and numerical errors when doing exp(log(u))"""
        u = np.exp(U)
        f = 1.0/(u+1/f_cp)
        return 2.1306 + 3.0*(1+1/eta_cp/u) - 3*U + piv * (1+q)**3 * g(f, q)
        
    def pv_of_U(self, U, piv, q):
        """Redefine pv with U=log(u) as variable, with u = 1/f - 1/f_cp. Prevents going over f_cp and numerical errors when doing exp(log(u))"""
        u = np.exp(U)
        f = 1.0/(u+1/f_cp)
        return 3/u + piv * h(f,q)



    

    

def coexistence(piv, q, fluid, solid, guess=[0.970, 1.185], Delta_muS_0=0.0):
    """return (f_Fluid, f_solid) at a given insersion work piv. 
    
    fluid, solid are respective EOS of the two phases.
    Delta_muS_0 is a constant that can be added to the solid chemical potential to ensure HS coexistence"""
    Us0 = np.log(1./np.array(guess) - 1/f_cp)
    result = fsolve(lambda Us: [
        fluid.pv_of_U(Us[0], piv, q) - solid.pv_of_U(Us[1], piv, q), 
        fluid.mu_of_U(Us[0], piv, q) - solid.mu_of_U(Us[1], piv, q) + Delta_muS_0
        ], Us0)
    return 1./(np.exp(result)+1/f_cp)
    
def all_coexistence(q, fluid, solid, pivs=None, maxpiv=None):
    """return (piv, f_Fluid, f_solid) at various insersion works pivs. If not given, pivs are sampled from 0 to maxpiv (default 2*critical pressure)."""
    Delta_muS_0 = solid.mu_of_U(np.log(1/1.185-1/f_cp), 0, q) - fluid.mu_of_U(np.log(1/0.970-1/f_cp), 0, q)
    if pivs is None:
        fc, pivc = fluid.critical_point(q)
        if maxpiv is None:
            maxpiv = pivc*2
        #need to integrate from top to bottom or it gets unstable
        pivs = np.union1d(
            np.linspace(0, 1.1*pivc), 
            np.linspace(1.1*pivc, maxpiv)
            )[::-1]
    topFS = [[1e-3, f_cp-1e-3]]
    for piv in pivs:
        topFS.append(coexistence(piv, q, fluid, solid, topFS[-1], Delta_muS_0))
    return np.vstack((
        [0, 0.970, 1.185],
        np.column_stack((pivs, topFS[1:]))[::-1]
        ))
