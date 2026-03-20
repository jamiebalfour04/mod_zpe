# mod_zpe.c

mod_zpe is an Apache module for the ZPE Programming Environment and the YASS language. Use this to add support for the YASS language into the Apache Web Server. 

## mod_zpe.runtime.c
<p>
  This version of mod_zpe is designed to run each script through the runtime and then close. It's slow but has more support.
</p>

## mod_zpe.pm.c
<p>
  This version of mod_zpe uses ZPE-PM and communicates using sockets. This is much faster but is only supported with version 1.14.4+.
</p>

## Usage

<p>
  If you have Apache's apxs command installed, use that to compile the file and install it into Apache:
</p>

<p><code>apxs -c -i mod_zpe.pm.c</code></p>

<p>If you don't have apxs installed, use the make command and install using the following commands:</p>

<ul>
  <li>
    Copy all files as listed into a folder on your computer
  </li>
  <li>
    Make sure you have the latest version of ZPE from GitHub.
  </li>
  <li>
    Copy zpe.jar into a folder in the root called <code>/zpe/</code> ()
  </li>
  <li>
    Make sure make.sh is executable (<code>chmod 755 make.sh</code>)
  </li>
  <li>
    Run the make.sh file with <code>./make.sh</code>
  </li>
  <li>
    Copy the resulting mod_zpe.so file into the appropriate directory (<code>cp mod_zpe.so /usr/lib/apache2/modules/</code>)
  </li>
  <li>
    Restart Apache (<code>httpd -k restart</code> or <code>apachectl -k restart</code>)
  </li>
  <li>
    Add the mod_zpe.load file into the /etc/apache2/mods-available folder with the following line:<br>
    <code>LoadModule zpe_module /usr/lib/apache2/modules/mod_zpe.so</code>
  </li>
  <li>
    Restart Apache (<code>httpd -k restart</code> or <code>apachectl -k restart</code>)
  </li>
</ul>

## Apache HTTPD setup
<pre>
&lt;VirtualHost *:80&gt;
  DocumentRoot /var/www/mywebsite/

  ServerName localhost
  &lt;Directory "/var/www/mywebsite/"&gt;
        AddHandler zpe .ywp .yas .yep .yex
  &lt;/Directory&gt;

&lt;/VirtualHost&gt;
</pre>
AddHandler zpe .yas
AddHandler zpe .ywp

## index.ywp

YASS Web Pages or YWP files work by embedding YASS code within <code>&lt;?ywp</code> and <code>ywp?&gt;</code>

<pre>

  &lt;!DOCTYPE html&gt;
  &lt;html&gt;
    &lt;head&gt;
    &lt;/head&gt;
    &lt;body&gt;
      &lt;?ywp
        print("Hello world!")
      ywp?&gt;
    &lt;/body&gt;    
  &lt;/html&gt;
  
</pre>
